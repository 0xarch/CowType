# SPDX-License-Identifier: GPL-3.0-or-later
"""Translation preferences; no credential is written to the ordinary config file."""
from pathlib import Path
import sys
import threading


def modules():
    folder = Path(__file__).resolve().parents[1] / 'translate'
    if not folder.exists():
        folder = Path(__file__).resolve().parents[2] / 'src-translate'
    sys.path.insert(0, str(folder))
    import translation_config
    import translation_credentials
    import translation_client
    return translation_config, translation_credentials, translation_client


def build(app, Adw, Gtk, GLib, tr, reload_fcitx):
    config, credentials, client = modules()
    group = app.group('translate', 'page.translate', tr('hint.translate'))
    enabled = Adw.SwitchRow(title=tr('setting.Translate'))
    enabled.set_active(app.values['Translate'].lower() == 'true')
    enabled.connect('notify::active', app.save)
    group.add(enabled)
    app.controls['Translate'] = enabled
    error = False
    try:
        values = config.load()
    except config.TranslationError:
        values, error = config.DEFAULTS.copy(), True
    rows = app.group('translate', 'translate.service')
    provider = Adw.ComboRow(title=tr('translate.provider'), model=Gtk.StringList.new([
        tr('translate.provider.bilibili'), tr('translate.provider.custom')]))
    provider.set_selected(0 if values['provider'] == 'bilibili' else 1)
    rows.add(provider)
    api = Adw.EntryRow(title=tr('translate.api')); api.set_text(values['api']); rows.add(api)
    model = Adw.EntryRow(title=tr('translate.model')); model.set_text(values['model']); rows.add(model)
    target = Adw.ComboRow(title=tr('translate.target'), model=Gtk.StringList.new([
        tr('translate.lang.' + language) for language in config.LANGUAGES]))
    target.set_selected(config.LANGUAGES.index(values['target'])); rows.add(target)
    key_sources = ('none', 'environment', 'secret')
    key_mode = Adw.ComboRow(title=tr('translate.key_source'), model=Gtk.StringList.new([
        tr('translate.key.' + source) for source in key_sources]))
    key_mode.set_selected(key_sources.index(values['key_source'])); rows.add(key_mode)
    key = Adw.PasswordEntryRow(title=tr('translate.key_value')); rows.add(key)
    status = Adw.ActionRow(title=tr('translate.error.config') if error else tr('translate.config_hint'))
    status.set_title_lines(0); rows.add(status)
    actions = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8, margin_top=12); rows.add(actions)
    save = Gtk.Button(label=tr('translate.save')); actions.append(save)
    check = Gtk.Button(label=tr('translate.check')); actions.append(check)
    forget = Gtk.Button(label=tr('translate.forget_key')); actions.append(forget)
    busy = False

    def visibility(*_):
        custom = provider.get_selected() == 1
        api.set_sensitive(custom); model.set_sensitive(custom)
        key_mode.set_sensitive(custom)
        key.set_visible(custom and key_sources[key_mode.get_selected()] == 'secret')
        forget.set_visible(custom and key_sources[key_mode.get_selected()] == 'secret')
        if not custom:
            api.set_text(config.BILIBILI_API); model.set_text(config.BILIBILI_MODEL); key_mode.set_selected(0)
            key.set_text('')
    provider.connect('notify::selected', visibility)
    key_mode.connect('notify::selected', visibility)
    visibility()
    if error:
        # An invalid file must remain available for manual repair.
        save.set_sensitive(False); check.set_sensitive(False); forget.set_sensitive(False)

    def run(operation):
        nonlocal busy
        if busy or error or app.load_error:
            return
        settings = dict(version=1, provider='bilibili' if provider.get_selected() == 0 else 'custom',
                        api=api.get_text().strip(), model=model.get_text().strip(),
                        target=config.LANGUAGES[target.get_selected()], key_source=key_sources[key_mode.get_selected()])
        try:
            config.validate(settings)
        except config.TranslationError as failure:
            status.set_title(tr(failure.code)); return
        password = key.get_text(); key.set_text('')
        busy = True; rows.set_sensitive(False); status.set_title(tr('translate.working'))
        def work():
            nonlocal password
            try:
                if operation == 'forget':
                    credentials.remove(settings); message = 'translate.key_removed'
                elif operation == 'save':
                    if settings['key_source'] == 'secret':
                        if password:
                            credentials.store(settings, password)
                        else:
                            credentials.resolve(settings)
                    config.save(settings)
                    reload_fcitx(); message = 'translate.saved'
                else:
                    loader = (lambda _: password) if password and settings['key_source'] == 'secret' else credentials.resolve
                    if password and not credentials.valid_key(password):
                        raise config.TranslationError('key')
                    client.translate('Hello, world!', settings, key_loader=loader)
                    message = 'translate.checked'
            except config.TranslationError as failure:
                message = failure.code
            except Exception:
                message = 'translate.error.config'
            password = ''
            def finish():
                nonlocal busy
                busy = False
                if not app.closed:
                    rows.set_sensitive(True); visibility(); status.set_title(tr(message))
                return False
            GLib.idle_add(finish)
        threading.Thread(target=work, name='cowtype-translation-settings', daemon=True).start()
    save.connect('clicked', lambda *_: run('save'))
    check.connect('clicked', lambda *_: run('check'))
    forget.connect('clicked', lambda *_: run('forget'))
    app.translation_provider = provider
    app.translation_key = key
    app.translation_target = target
    app.translation_key_source = key_mode
