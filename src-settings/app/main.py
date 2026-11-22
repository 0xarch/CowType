#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Libadwaita preferences. Changes are saved immediately; downloads are explicit."""
import argparse
import importlib.util
import os
import signal
from pathlib import Path
import subprocess
import sys
import threading
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'i18n'))
from catalog import tr, en
from translation import build as build_translation
from memory import show as show_memory
from custom import show as show_custom

PAGES = [('input', 'preferences-system-symbolic'), ('keys', 'input-keyboard-symbolic'),
         ('voice', 'audio-input-microphone-symbolic'), ('translate', 'accessories-dictionary-symbolic'),
         ('words', 'view-list-symbolic')]

def manager_module():
    path = Path(__file__).resolve().parents[1] / 'dictionaries/manage.py'
    if not path.exists():
        path = Path(__file__).resolve().parents[2] / 'src-data-tools/dictionaries/manage.py'
    spec = importlib.util.spec_from_file_location('cowtype_dictionary_manager', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def settings_module():
    path = Path(__file__).resolve().parents[1] / 'config/settings.py'
    spec = importlib.util.spec_from_file_location('cowtype_settings', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def memory_store_module():
    path = Path(__file__).resolve().parents[1] / 'config/memory.py'
    spec = importlib.util.spec_from_file_location('cowtype_memory_store', path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def custom_store_module():
    path = Path(__file__).resolve().parents[1] / 'config/custom.py'
    spec = importlib.util.spec_from_file_location('cowtype_custom_store', path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def reload_fcitx():
    try:
        result = subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.fcitx.Fcitx5',
                                 '--object-path', '/controller', '--method', 'org.fcitx.Fcitx.Controller1.ReloadAddonConfig',
                                 'cowtype'], capture_output=True, timeout=8, check=False)
        return result.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--page', default='input')
    parser.add_argument('--smoke-test', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        import gi
        gi.require_version('Gtk', '4.0')
        gi.require_version('Adw', '1')
        from gi.repository import Adw, Gtk, Gio, GLib
    except (ImportError, ValueError):
        parser.exit(1, en('error.dependencies') + '\n')
    manager, config = manager_module(), settings_module()
    data_home = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))

    class Settings(Adw.Application):
        def __init__(self):
            super().__init__(application_id='io.github._0xarch.CowType.Config', flags=Gio.ApplicationFlags.NON_UNIQUE)
            self.busy = False
            self.cancel = threading.Event()
            self.closed = False
            self.controls = {}
            self.voice_process = None
            self.ready = False

        def group(self, page, title, description=None):
            group = Adw.PreferencesGroup(title=tr(title) if title else '', description=description)
            self.pages[page].add(group)
            return group

        def action(self, group, key, callback, subtitle=None, destructive=False):
            row = Adw.ActionRow(title=tr(key), subtitle=subtitle, activatable=True)
            icon = Gtk.Image.new_from_icon_name('go-next-symbolic')
            row.add_suffix(icon)
            if destructive:
                row.add_css_class('error')
            row.connect('activated', callback)
            group.add(row)
            return row

        def status_row(self, group, key):
            row = Adw.ActionRow(title=tr(key))
            row.set_title_lines(0)
            group.add(row)
            return row

        def toast(self, text):
            if getattr(self, 'last_toast', None): self.last_toast.dismiss()
            self.last_toast = Adw.Toast.new(text)
            self.overlay.add_toast(self.last_toast)

        def failure(self, error):
            self.toast(tr('error.operation', error=str(error)))

        def do_activate(self):
            Gtk.Window.set_default_icon_name('cowtype')
            self.window = Adw.ApplicationWindow(application=self, title=tr('app.title'),
                default_width=760, default_height=680)
            self.host = self.window
            self.window.connect('close-request', self.close)
            self.stack = Adw.ViewStack()
            toolbar = Adw.ToolbarView()
            header = Adw.HeaderBar()
            switcher = Adw.ViewSwitcher(stack=self.stack, policy=Adw.ViewSwitcherPolicy.NARROW)
            header.set_title_widget(switcher)
            toolbar.add_top_bar(header)
            toolbar.set_content(self.stack)
            self.overlay = Adw.ToastOverlay()
            self.overlay.set_child(toolbar)
            self.window.set_content(self.overlay)
            self.pages = {}
            for name, icon in PAGES:
                page = Adw.PreferencesPage(name=name, title=tr('page.' + name), icon_name=icon)
                stack_page = self.stack.add_titled(page, name, tr('page.' + name))
                stack_page.set_icon_name(icon)
                self.pages[name] = page
            try:
                self.values = config.load()
                self.load_error = ''
            except Exception as error:
                self.values = config.DEFAULTS.copy()
                self.load_error = tr('error.load', error=str(error))
            general = self.group('input', 'group.appearance')
            pinyin = self.group('input', 'group.pinyin')
            self.emoji_options = self.group('input', 'group.emoji')
            privacy = self.group('input', 'group.privacy')
            keys = self.group('keys', 'page.keys')
            voice = self.group('voice', 'group.voice')
            groups = {'Emoji': self.emoji_options, 'EmojiFirst': self.emoji_options, 'UnicodePrefix': general, 'Punctuation': general, 'Horizontal': general, 'RememberMode': keys,
                'StatisticalModel': pinyin, 'Sentence': pinyin, 'Correction': pinyin, 'Completion': pinyin, 'Initials': pinyin, 'ShowPinyin': general,
                'Learning': privacy, 'Voice': voice, 'VoiceResident': voice, 'VoiceAlwaysResident': voice, 'PageBrackets': keys, 'PageMinusEqual': keys}
            for key, group in groups.items():
                row = Adw.SwitchRow(title=tr('setting.' + key))
                if key == 'Emoji': row.set_subtitle(tr('hint.emoji'))
                if key == 'EmojiFirst': row.set_subtitle(tr('hint.emoji_first'))
                if key == 'UnicodePrefix': row.set_subtitle(tr('hint.unicode_prefix'))
                if key == 'RememberMode': row.set_subtitle(tr('hint.remember_mode'))
                if key == 'StatisticalModel': row.set_subtitle(tr('hint.statistical_model'))
                if key == 'Initials': row.set_subtitle(tr('hint.initials'))
                if key == 'VoiceResident': row.set_subtitle(tr('hint.resident'))
                if key == 'VoiceAlwaysResident': row.set_subtitle(tr('hint.always_resident'))
                row.set_active(self.values[key].lower() == 'true')
                row.connect('notify::active', self.save)
                group.add(row)
                self.controls[key] = row
            picker = Adw.SwitchRow(title=tr('setting.EmojiSkinTonePicker'), subtitle=tr('hint.emoji_skin_tone_picker'))
            picker.set_active(self.values['EmojiSkinTonePicker'].lower() == 'true')
            picker.connect('notify::active', self.save)
            self.emoji_options.add(picker)
            self.controls['EmojiSkinTonePicker'] = picker
            self.skin_tone_row = Adw.ActionRow(title=tr('setting.EmojiDefaultSkinTone'))
            tones = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8, valign=Gtk.Align.CENTER)
            self.skin_tone_buttons = []
            for tone, label in enumerate(('👍', '👍🏻', '👍🏼', '👍🏽', '👍🏾', '👍🏿')):
                button = Gtk.CheckButton(label=label)
                if self.skin_tone_buttons: button.set_group(self.skin_tone_buttons[0])
                button.set_active(tone == int(self.values['EmojiDefaultSkinTone']))
                button.connect('toggled', lambda widget: self.save() if widget.get_active() else None)
                self.skin_tone_buttons.append(button)
                tones.append(button)
            self.skin_tone_row.add_suffix(tones)
            self.emoji_options.add(self.skin_tone_row)
            self.page_size = Adw.SpinRow.new_with_range(3, 9, 1)
            self.page_size.set_title(tr('setting.PageSize'))
            self.page_size.set_value(int(self.values['PageSize']))
            self.page_size.connect('notify::value', self.save)
            general.add(self.page_size)
            self.feature_hotkeys = {}
            for name, (_, choices) in config.SHORTCUTS.items():
                shortcut = Adw.ComboRow(title=tr('setting.' + name),
                    model=Gtk.StringList.new([tr('hotkey.' + key) if key not in tuple(f'F{i}' for i in range(1, 13)) else key for key in choices]))
                shortcut.set_subtitle(tr('hint.shortcuts'))
                shortcut.set_selected(choices.index(self.values[name]))
                shortcut.connect('notify::selected', self.save)
                self.feature_hotkeys[name] = shortcut
                keys.add(shortcut)
            self.hotkey = self.feature_hotkeys['VoiceHotkey']
            hold = Adw.SwitchRow(title=tr('setting.VoiceModifierHold'), subtitle=tr('hint.modifier_hold'))
            hold.set_active(self.values['VoiceModifierHold'].lower() == 'true')
            hold.connect('notify::active', self.save)
            keys.add(hold)
            self.controls['VoiceModifierHold'] = hold
            self.update_visibility()
            integration = self.group('input', 'group.integration', tr('hint.fcitx'))
            self.action(integration, 'action.fcitx', lambda *_: self.spawn(['fcitx5-configtool']))
            self.action(integration, 'app.about', self.about)
            self.memory_action = self.action(privacy, 'learning.manage', lambda *_: show_memory(self, memory_store_module(), config, Adw, Gtk, GLib, tr))
            self.action(privacy, 'action.clear', self.confirm_clear, destructive=True)
            model = self.group('voice', 'group.model', tr('hint.model'))
            self.model_path = Adw.ActionRow(title=tr('group.model'), subtitle=str(config.model_dir()))
            self.model_path.set_subtitle_lines(0)
            model.add(self.model_path)
            buttons = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8,
                margin_top=12)
            model.add(buttons)
            def model_button(key, callback):
                button = Gtk.Button(label=tr(key))
                button.connect('clicked', callback)
                buttons.append(button)
                return button
            self.choose = model_button('action.choose', self.choose_model)
            self.check = model_button('action.check', lambda *_: self.voice_action(False))
            self.prepare = model_button('action.download', self.download_or_cancel)
            self.preparing = False
            build_translation(self, Adw, Gtk, GLib, tr, reload_fcitx)
            custom_words = self.group('words', 'custom.title', tr('custom.hint'))
            self.custom_action = self.action(custom_words, 'custom.manage', lambda *_: show_custom(self, custom_store_module(), config, Adw, Gtk, GLib, tr))
            system_words = self.group('words', 'group.system_dictionary')
            self.system_dictionary = Adw.SwitchRow(title=tr('setting.SystemDictionary'), subtitle=tr('hint.system_dictionary'))
            self.system_dictionary.set_active(self.values['SystemDictionary'].lower() == 'true')
            self.system_dictionary.connect('notify::active', self.save)
            self.controls['SystemDictionary'] = self.system_dictionary
            system_words.add(self.system_dictionary)
            words = self.group('words', 'group.words', tr('hint.words'))
            profile = manager.registry()['wanxiang']
            self.action(words, 'action.words.source', lambda *_: Gtk.UriLauncher.new('https://github.com/' + profile['repository']).launch(self.host, None, None),
                tr('hint.words.source', revision=profile['revision'], size=sum(r['bytes'] for r in profile['files']) // 1024**2))
            self.install = self.action(words, 'action.words.install', lambda *_: self.operate(True))
            self.remove = self.action(words, 'action.words.remove', lambda *_: self.operate(False))
            self.status = self.status_row(words, 'status.words.absent')
            self.refresh_status()
            if self.load_error:
                error_group = self.group('input', None)
                error_group.add(Adw.ActionRow(title=self.load_error))
                for row in [*self.controls.values(), *self.feature_hotkeys.values(), self.page_size, self.hotkey, *self.skin_tone_buttons]: row.set_sensitive(False)
            self.ready = True
            self.stack.set_visible_child_name(args.page if args.page in self.pages else 'input')
            self.window.present()
            if args.smoke_test:
                self.smoke_passed = False
                def check_visible():
                    assert len(self.pages) == 5 and len(self.controls) == 21
                    assert not self.controls['Translate'].get_active()
                    assert self.translation_provider.get_selected() == 0
                    assert self.translation_bilibili_model.get_model().get_n_items() == 3
                    assert self.translation_target.get_selected() == 0
                    assert not self.translation_key.get_visible()
                    assert self.controls['UnicodePrefix'].get_visible()
                    assert self.controls['UnicodePrefix'].get_active()
                    assert self.memory_action.get_visible()
                    assert self.controls['Emoji'].get_visible()
                    assert not self.controls['Emoji'].get_active()
                    assert self.emoji_options.get_visible()
                    assert self.controls['Emoji'].is_ancestor(self.emoji_options)
                    assert self.controls['EmojiFirst'].is_ancestor(self.emoji_options)
                    assert self.controls['EmojiSkinTonePicker'].is_ancestor(self.emoji_options)
                    assert not self.controls['EmojiFirst'].get_active()
                    assert not self.controls['EmojiFirst'].get_visible()
                    assert not self.skin_tone_row.get_visible()
                    assert self.controls['EmojiSkinTonePicker'].get_active()
                    assert len(self.skin_tone_buttons) == 6
                    assert self.skin_tone_buttons[0].get_active()
                    self.ready = False
                    self.controls['Emoji'].set_active(True)
                    self.update_visibility()
                    assert self.emoji_options.get_visible()
                    assert self.controls['EmojiFirst'].get_visible()
                    assert self.controls['EmojiSkinTonePicker'].get_visible()
                    self.controls['EmojiFirst'].set_active(True)
                    self.controls['EmojiFirst'].set_active(False)
                    self.skin_tone_buttons[3].set_active(True)
                    assert self.skin_tone_buttons[3].get_active()
                    assert not self.skin_tone_buttons[0].get_active()
                    self.controls['EmojiSkinTonePicker'].set_active(False)
                    self.update_visibility()
                    assert self.skin_tone_row.get_visible()
                    self.skin_tone_buttons[0].set_active(True)
                    self.controls['EmojiSkinTonePicker'].set_active(True)
                    self.controls['Emoji'].set_active(False)
                    self.update_visibility()
                    self.ready = True
                    assert self.controls['StatisticalModel'].get_visible()
                    assert not self.controls['StatisticalModel'].get_active()
                    assert len(self.feature_hotkeys) == 5
                    assert self.feature_hotkeys['InternalCodeHotkey'].get_selected() == 6
                    assert self.hotkey.get_model().get_n_items() == len(config.VOICE_HOTKEYS)
                    assert self.get_windows() == [self.window]
                    assert self.window.get_mapped() and self.stack.get_mapped()
                    assert isinstance(self.overlay, Adw.ToastOverlay)
                    assert not self.controls['VoiceModifierHold'].get_visible()
                    self.ready = False
                    self.translation_bilibili_model.set_selected(0)
                    assert self.translation_bilibili_model.get_visible()
                    self.translation_bilibili_model.set_selected(2)
                    self.translation_provider.set_selected(1)
                    self.translation_api.set_text('https://synthetic.invalid/v1')
                    self.translation_model.set_text('synthetic-model')
                    self.translation_key_source.set_selected(2)
                    assert self.translation_key.get_visible()
                    self.translation_key.set_text('synthetic-unsaved-key')
                    self.translation_target.set_selected(3)
                    self.translation_provider.set_selected(0)
                    assert not self.translation_key.get_visible()
                    assert not self.translation_key.get_text()
                    self.translation_bilibili_model.set_selected(1)
                    self.translation_provider.set_selected(1)
                    assert self.translation_api.get_text() == 'https://synthetic.invalid/v1'
                    assert self.translation_model.get_text() == 'synthetic-model'
                    assert self.translation_key_source.get_selected() == 2
                    self.translation_provider.set_selected(0)
                    self.translation_bilibili_model.set_selected(2)
                    self.translation_target.set_selected(0)
                    assert self.controls['RememberMode'].get_visible()
                    self.hotkey.set_selected(config.VOICE_HOTKEYS.index('Alt_L'))
                    self.update_visibility()
                    assert self.controls['VoiceModifierHold'].get_visible()
                    self.hotkey.set_selected(config.VOICE_HOTKEYS.index('F8'))
                    self.controls['VoiceResident'].set_active(False)
                    self.update_visibility()
                    assert not self.controls['VoiceAlwaysResident'].get_visible()
                    self.controls['VoiceResident'].set_active(True)
                    self.update_visibility()
                    assert self.controls['VoiceAlwaysResident'].get_visible()
                    assert self.prepare.get_label() == tr('action.download')
                    memory_dialog = show_memory(self, memory_store_module(), config, Adw, Gtk, GLib, tr)
                    assert memory_dialog.get_visible()
                    def memory_row(widget):
                        if isinstance(widget, Adw.ActionRow):
                            return widget
                        child = widget.get_first_child()
                        while child:
                            found = memory_row(child)
                            if found: return found
                            child = child.get_next_sibling()
                        return None
                    record_row = memory_row(memory_dialog)
                    if record_row is not None:
                        record_row.emit('activated')
                        editor = self.window.get_visible_dialog()
                        assert editor.get_title() == tr('learning.edit')
                        editor.close()
                        print('Memory editor opened with synthetic record', flush=True)
                    memory_dialog.close()
                    assert self.custom_action.get_visible()
                    custom_store = custom_store_module()
                    before_custom = custom_store.load(config.data_home())
                    custom_dialog = show_custom(self, custom_store, config, Adw, Gtk, GLib, tr)
                    def find_custom(widget, predicate):
                        if predicate(widget): return widget
                        child = widget.get_first_child()
                        while child:
                            found = find_custom(child, predicate)
                            if found: return found
                            child = child.get_next_sibling()
                        return None
                    add_custom = find_custom(custom_dialog, lambda w: isinstance(w, Gtk.Button) and w.get_label() == tr('custom.add'))
                    add_custom.emit('clicked')
                    custom_editor = self.window.get_visible_dialog()
                    find_custom(custom_editor, lambda w: isinstance(w, Adw.EntryRow)).set_text('smoketest')
                    find_custom(custom_editor, lambda w: isinstance(w, Gtk.TextView)).get_buffer().set_text('合成自定义词条\n第二行')
                    find_custom(custom_editor, lambda w: isinstance(w, Adw.SpinRow)).set_value(321)
                    custom_editor.close()  # flush pending autosave on close
                    saved_custom = custom_store.Record('smoketest', '合成自定义词条\n第二行', 321)
                    assert saved_custom in custom_store.load(config.data_home())
                    entry_custom = find_custom(custom_dialog, lambda w: isinstance(w, Adw.ActionRow) and w.get_title() == 'smoketest')
                    entry_custom.emit('activated')
                    custom_editor = self.window.get_visible_dialog()
                    find_custom(custom_editor, lambda w: isinstance(w, Gtk.Button) and w.get_label() == tr('action.delete')).emit('clicked')
                    assert custom_store.load(config.data_home()) == before_custom
                    custom_dialog.close()
                    print('Custom dictionary autosave and deletion passed', flush=True)
                    self.smoke_passed = True
                    print('Libadwaita smoke test: single application window and preferences remain mapped after startup', flush=True)
                    self.window.close()
                    self.quit()
                    return False
                GLib.timeout_add(1000, check_visible)

        def update_visibility(self):
            enabled = self.controls['Emoji'].get_active()
            self.controls['EmojiFirst'].set_visible(enabled)
            self.controls['EmojiSkinTonePicker'].set_visible(enabled)
            self.skin_tone_row.set_visible(enabled)
            self.controls['VoiceModifierHold'].set_visible(config.VOICE_HOTKEYS[self.hotkey.get_selected()] not in ('None', *tuple(f'F{i}' for i in range(1, 13))))
            self.controls['VoiceAlwaysResident'].set_visible(self.controls['VoiceResident'].get_active())

        def save(self, *_):
            if not self.ready or self.load_error: return
            self.update_visibility()
            try:
                values = self.values.copy()
                values['EmojiDefaultSkinTone'] = str(next(i for i, button in enumerate(self.skin_tone_buttons) if button.get_active()))
                values['PageSize'] = str(int(self.page_size.get_value()))
                values.update({name: config.SHORTCUTS[name][1][row.get_selected()] for name, row in self.feature_hotkeys.items()})
                values.update({key: str(row.get_active()) for key, row in self.controls.items()})
                config.save(values)
                self.values = values
                self.toast(tr('status.saved' if reload_fcitx() else 'status.saved.offline'))
            except Exception as error:
                self.failure(error)

        def question(self, title, body, confirm, callback, destructive=False):
            dialog = Adw.AlertDialog(heading=tr(title), body=tr(body))
            dialog.add_response('cancel', tr('action.cancel'))
            dialog.add_response('confirm', tr(confirm))
            dialog.set_close_response('cancel')
            dialog.set_default_response('cancel')
            dialog.set_response_appearance('confirm', Adw.ResponseAppearance.DESTRUCTIVE if destructive else Adw.ResponseAppearance.SUGGESTED)
            dialog.connect('response', lambda _d, response: callback() if response == 'confirm' else None)
            dialog.present(self.window)

        def confirm_clear(self, *_):
            def clear():
                try:
                    config.clear_learning()
                    self.toast(tr('status.cleared'))
                except Exception as error: self.failure(error)
            self.question('dialog.clear.title', 'dialog.clear.body', 'action.delete', clear, True)

        def choose_model(self, *_):
            dialog = Gtk.FileDialog(title=tr('model.choose'))
            def selected(d, result):
                try:
                    directory = d.select_folder_finish(result).get_path()
                    config.select_model(directory)
                    self.model_path.set_subtitle(str(config.model_dir()))
                    self.toast(tr('status.model.selected'))
                    reload_fcitx()
                except GLib.Error: pass
                except Exception as error: self.failure(error)
            dialog.select_folder(self.host, None, selected)

        def download_or_cancel(self, *_):
            if self.preparing: self.cancel_voice_action()
            else: self.confirm_download()

        def confirm_download(self, *_):
            self.question('dialog.download.title', 'dialog.download.body', 'action.continue', lambda: self.voice_action(True))

        def cancel_voice_action(self):
            if self.voice_process and self.voice_process.poll() is None:
                try: os.killpg(self.voice_process.pid, signal.SIGTERM)
                except ProcessLookupError: pass

        def voice_action(self, prepare):
            if self.voice_process and self.voice_process.poll() is None: return
            if prepare:
                script = Path(__file__).resolve().parents[1] / 'paraformer/prepare_paraformer.py'
                if not script.exists(): script = Path(__file__).resolve().parents[2] / 'src-voice/paraformer/prepare_paraformer.py'
                command = [sys.executable, str(script), '--disable-enable', '--disable-reload']
            else:
                command = ['cowtype-voice', '--check', '--dir', str(config.model_dir())]
            try:
                # Third-party preparation output is consumed privately; it is never a host log.
                env = dict(os.environ, COWTYPE_LANGUAGE='en_US', LANG='C.UTF-8')
                self.voice_process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True, env=env)
            except OSError as error:
                self.failure(error)
                return
            process = self.voice_process
            self.preparing = prepare
            self.prepare.set_sensitive(prepare)
            self.prepare.set_label(tr('action.cancel.download' if prepare else 'action.download'))
            self.check.set_sensitive(False)
            self.choose.set_sensitive(False)
            self.toast(tr('status.working'))
            def work():
                code = process.wait()
                def done():
                    if not self.closed:
                        self.model_path.set_subtitle(str(config.model_dir()))
                        self.toast(tr('status.model.done') if code == 0 else tr('status.model.cancelled') if code < 0 else tr('status.model.failed', code=code))
                        if code == 0: reload_fcitx()
                        self.preparing = False
                        self.prepare.set_label(tr('action.download'))
                        self.prepare.set_sensitive(True)
                        self.check.set_sensitive(True)
                        self.choose.set_sensitive(True)
                    return False
                GLib.idle_add(done)
            threading.Thread(target=work, name='cowtype-model-operation').start()

        def refresh_status(self, message=''):
            installed = manager.managed_path(data_home).exists()
            self.install.set_sensitive(not self.busy and not installed)
            self.remove.set_sensitive(not self.busy and installed)
            self.status.set_title(message or tr('status.words.present' if installed else 'status.words.absent'))

        def operate(self, install):
            if self.busy: return
            self.busy = True
            self.cancel.clear()
            self.refresh_status(tr('status.working'))
            def work():
                try:
                    if install: manager.install_optional(data_home, cancel=self.cancel)
                    else: manager.remove_optional(data_home)
                    reload_fcitx()
                    text = tr('status.words.done')
                except Exception as error: text = tr('error.operation', error=str(error))
                def finish():
                    self.busy = False
                    if not self.closed: self.refresh_status(text)
                    return False
                GLib.idle_add(finish)
            threading.Thread(target=work, name='cowtype-dictionary-operation').start()

        def spawn(self, command):
            try: subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except OSError: self.toast(tr('error.spawn'))

        def about(self, *_):
            dialog = Adw.AboutDialog(application_name=tr('app.name'), application_icon='cowtype',
                version='0.4.1', license_type=Gtk.License.GPL_3_0, comments=tr('about.comments'),
                website='https://github.com/0xarch/CowType', issue_url='https://github.com/0xarch/CowType/issues')
            dialog.present(self.window)

        def close(self, *_):
            self.closed = True
            self.cancel.set()
            self.cancel_voice_action()
            return False

    app = Settings()
    result = app.run([sys.argv[0]])
    if args.smoke_test and not getattr(app, 'smoke_passed', False):
        print('Settings startup test failed: window did not remain mapped', file=sys.stderr)
        return 1
    return result


if __name__ == '__main__':
    sys.exit(main())
