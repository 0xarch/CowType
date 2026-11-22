# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic HTTP/Secret Service contracts. No production keys, endpoints or keyring."""
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src-translate'))
import translation_config as config
import translation_client as client
import translation_credentials as credentials


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass
    def do_POST(self):
        self.server.requests.append((self.path, self.headers.get('Authorization'), json.loads(self.rfile.read(int(self.headers['Content-Length'])))))
        self.send_response(self.server.status)
        if self.server.status == 302:
            self.send_header('Location', self.server.redirect)
        self.end_headers()
        self.wfile.write(self.server.body)


class TranslationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.env = patch.dict(os.environ, XDG_CONFIG_HOME=self.tmp.name,
                              COWTYPE_TRANSLATE_KEY='fixture-key', COWTYPE_TRANSLATE_API='https://ignored.invalid/v1',
                              COWTYPE_TRANSLATE_MODEL='ignored-model')
        self.env.start()
        self.server = ThreadingHTTPServer(('127.0.0.1',0), Handler)
        self.server.status = 200
        self.server.requests = []
        self.server.body = json.dumps({'choices':[{'finish_reason':'stop','message':{'content':'合成译文'}}]}).encode()
        self.thread = threading.Thread(target=self.server.serve_forever,daemon=True); self.thread.start()
        self.settings = dict(config.DEFAULTS, provider='custom',api=f'http://127.0.0.1:{self.server.server_port}/v1',
                             model='fixture-model', key_source='environment')
    def tearDown(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join()
        self.env.stop(); self.tmp.cleanup()
    def test_default_and_roundtrip_ignore_api_and_model_environment(self):
        self.assertEqual(config.load(), config.DEFAULTS)
        values=dict(self.settings,target='ja')
        config.save(values)
        self.assertEqual(config.load(),values)
        self.assertEqual(config.path().stat().st_mode & 0o777,0o600)
        self.assertNotIn('fixture-key', config.path().read_text())
        with self.assertRaises(config.TranslationError): config.save(dict(values,api_key='never store'))
    def test_invalid_config_preserved_and_symlink_rejected(self):
        config.path().parent.mkdir()
        config.path().write_text('{broken')
        with self.assertRaises(config.TranslationError): config.load()
        self.assertEqual(config.path().read_text(),'{broken')
        config.path().unlink();config.path().symlink_to(Path(self.tmp.name)/'outside')
        with self.assertRaises(config.TranslationError):config.save(self.settings)
    def test_url_validation_and_completion_path(self):
        self.assertEqual(config.endpoint('https://example.invalid/v1/'),'https://example.invalid/v1/chat/completions')
        self.assertEqual(config.endpoint('https://example.invalid/v1/chat/completions'),'https://example.invalid/v1/chat/completions')
        self.assertEqual(config.endpoint('http://[::1]:123/v1'),'http://[::1]:123/v1/chat/completions')
        for url in ['http://example.invalid/v1','https://user:key@example.invalid','https://example.invalid?key=abc',
                    'https://example.invalid/#key','file:///tmp/key','https://example.invalid/\n','https://example.invalid:bad']:
            with self.subTest(url=url),self.assertRaises(config.TranslationError):config.endpoint(url)
    def test_http_contract_fixed_target_and_key(self):
        self.assertEqual(client.translate('synthetic source',dict(self.settings,target='ja')),'合成译文')
        path,key,payload=self.server.requests[0]
        self.assertEqual(path,'/v1/chat/completions');self.assertEqual(key,'Bearer fixture-key')
        self.assertEqual(payload['model'],'fixture-model')
        self.assertIn('日语',payload['messages'][0]['content'])
        self.assertIn('synthetic source',payload['messages'][0]['content'])
        self.assertNotIn('chat_template_kwargs',payload)
        self.assertFalse(payload['stream'])
    def test_all_targets_and_automatic_direction_prompt(self):
        for language in config.LANGUAGES:
            config.validate(dict(self.settings,target=language))
        client.translate('synthetic source',self.settings)
        prompt=self.server.requests[-1][2]['messages'][0]['content']
        self.assertIn('中文文本翻译为英语',prompt);self.assertIn('其他语言文本翻译为简体中文',prompt)
    def test_preset_is_keyless_and_disables_thinking(self):
        class Response(io.BytesIO):pass
        class Opener:
            def open(inner,request,timeout):
                self.assertEqual(request.full_url,config.BILIBILI_API+'/chat/completions')
                self.assertIsNone(request.get_header('Authorization'))
                payload=json.loads(request.data)
                self.assertEqual(payload['model'],config.BILIBILI_MODEL)
                self.assertEqual(payload['chat_template_kwargs'],{'enable_thinking':False})
                return Response(self.server.body)
        client.translate('synthetic',config.DEFAULTS,opener=Opener())
        self.assertEqual(self.server.requests,[])
    def test_http_errors_do_not_expose_response_or_credentials(self):
        for status,code in [(401,'auth'),(403,'auth'),(429,'rate'),(500,'http'),(503,'unavailable')]:
            self.server.status=status;self.server.body=b'private fixture-key synthetic source'
            with self.assertRaises(config.TranslationError) as caught:client.translate('synthetic source',self.settings)
            self.assertEqual(str(caught.exception),'translate.error.'+code)
    def test_redirect_never_forwards_authorization_or_text(self):
        self.server.status=302;self.server.redirect=f'http://127.0.0.1:{self.server.server_port}/other'
        with self.assertRaises(config.TranslationError):client.translate('synthetic source',self.settings)
        self.assertEqual(len(self.server.requests),1)
    def test_reject_incomplete_empty_invalid_and_oversize_responses(self):
        for body in [b'not-json',b'{}',json.dumps({'choices':[{'finish_reason':'length','message':{'content':'partial'}}]}).encode(),
                     json.dumps({'choices':[{'message':{'content':'<think>reason</think>translation'}}]}).encode(),
                     json.dumps({'choices':[{'message':{'content':''}}]}).encode(),
                     json.dumps({'choices':[{'message':{'content':'\x00'}}]}).encode(),b'x'*(client.MAX_RESPONSE+1)]:
            self.server.body=body
            with self.assertRaises(config.TranslationError):client.translate('synthetic',self.settings)
    def test_input_and_key_validation_prevents_requests(self):
        for text in ['', ' ', '\x00', 'x'*(client.MAX_INPUT+1)]:
            with self.assertRaises(config.TranslationError):client.translate(text,self.settings)
        for key in ['', 'injected\nheader']:
            with patch.dict(os.environ,COWTYPE_TRANSLATE_KEY=key),self.assertRaises(config.TranslationError):
                client.translate('synthetic',self.settings)
        self.assertEqual(self.server.requests,[])
    def test_timeout_is_sanitized(self):
        class Timeout:
            def open(self,*_,**__):raise TimeoutError('private fixture-key')
        with self.assertRaisesRegex(config.TranslationError,'translate.error.timeout'):
            client.translate('synthetic',self.settings,opener=Timeout())
    def test_secret_service_roundtrip_and_endpoint_binding(self):
        class Secret:
            COLLECTION_DEFAULT='default'
            saved={}
            @classmethod
            def password_store_sync(cls,schema,attributes,collection,label,key,cancel):
                cls.saved[attributes['endpoint']]=key;return True
            @classmethod
            def password_lookup_sync(cls,schema,attributes,cancel):return cls.saved.get(attributes['endpoint'])
            @classmethod
            def password_clear_sync(cls,schema,attributes,cancel):cls.saved.pop(attributes['endpoint'],None)
        settings=dict(self.settings,key_source='secret')
        with patch.object(credentials,'_service',return_value=(Secret,object())):
            credentials.store(settings,'fixture-secret')
            self.assertEqual(credentials.resolve(settings),'fixture-secret')
            with self.assertRaises(config.TranslationError):credentials.resolve(dict(settings,api='https://other.invalid'))
            credentials.remove(settings)
            with self.assertRaises(config.TranslationError):credentials.resolve(settings)
        self.assertFalse(config.path().exists())
    def test_keyring_failure_has_no_plaintext_fallback(self):
        settings=dict(self.settings,key_source='secret')
        with patch.object(credentials,'_service',side_effect=config.TranslationError('keyring')):
            with self.assertRaises(config.TranslationError):credentials.store(settings,'fixture-secret')
        self.assertFalse(config.path().exists())
    def test_one_shot_helper_uses_only_isolated_settings(self):
        config.save(self.settings)
        helper=Path(__file__).resolve().parents[1]/'src-translate/main.py'
        result=subprocess.run([sys.executable,str(helper)],input='synthetic'.encode().hex()+'\n',text=True,capture_output=True,timeout=5)
        self.assertEqual(result.returncode,0);self.assertEqual(result.stderr,'')
        self.assertEqual(bytes.fromhex(result.stdout.split('\t')[1]).decode(),'合成译文')
        self.assertNotIn('fixture-key',result.stdout)

if __name__=='__main__':unittest.main()
