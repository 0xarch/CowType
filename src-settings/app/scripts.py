# SPDX-License-Identifier: GPL-3.0-or-later
"""Lua permissions and script editor; code runs only through enabled triggers."""
import importlib.util
from pathlib import Path


def build(app, Adw, Gtk, tr, config, reload_fcitx):
    path = Path(__file__).resolve().parents[1] / 'config/scripts.py'
    spec = importlib.util.spec_from_file_location('cowtype_scripts', path)
    store = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(store)
    modes = ('Off', 'Strict', 'Full')
    group = app.group('scripts', 'scripts.permissions', tr('scripts.hint'))
    mode = Adw.ComboRow(title=tr('scripts.mode'), model=Gtk.StringList.new([tr('scripts.' + name) for name in modes]))
    mode.set_selected(modes.index(app.values['ScriptMode']))
    group.add(mode)
    warning = Adw.ActionRow(title=tr('scripts.warning_title'), subtitle=tr('scripts.warning'))
    warning.add_css_class('warning')
    warning.set_subtitle_lines(0)
    warning.set_visible(mode.get_selected() == 2)
    group.add(warning)
    listing = app.group('scripts', 'scripts.manage', tr('scripts.contract'))
    state = {'records': [], 'rows': []}

    def persist():
        store.save(config.config_home(), config.atomic, state['records'])
        app.toast(tr('status.saved' if reload_fcitx() else 'status.saved.offline'))

    def change_mode(*_):
        selected = modes[mode.get_selected()]
        previous = app.values['ScriptMode']
        def apply(value):
            app.values['ScriptMode'] = value
            config.save(app.values)
            warning.set_visible(value == 'Full')
            reload_fcitx()
        if selected == 'Full' and previous != 'Full':
            mode.set_sensitive(False)
            dialog = Adw.AlertDialog(heading=tr('scripts.warning_title'), body=tr('scripts.warning'))
            dialog.add_response('cancel', tr('action.cancel'))
            dialog.add_response('enable', tr('scripts.enable_full'))
            dialog.set_response_appearance('enable', Adw.ResponseAppearance.DESTRUCTIVE)
            dialog.set_close_response('cancel')
            def response(_dialog, value):
                try:
                    if value == 'enable': apply('Full')
                    else: mode.set_selected(modes.index(previous))
                except Exception as error: app.failure(error)
                finally: mode.set_sensitive(True)
            dialog.connect('response', response)
            dialog.present(app.window)
        else:
            try: apply(selected)
            except Exception as error: app.failure(error)
    mode.connect('notify::selected', change_mode)

    def check_message(error):
        key = getattr(error, 'key', str(error))
        message = tr(key if key.startswith('scripts.') else 'scripts.error')
        line = getattr(error, 'line', 0)
        if line: message += '\n' + tr('scripts.check.line', line=line)
        return message

    def report(title, body, parent=None, proceed=None, save_anyway=False):
        alert = Adw.AlertDialog(heading=tr(title), body=body)
        alert.add_response('cancel', tr('action.cancel'))
        alert.set_close_response('cancel')
        if proceed:
            alert.add_response('continue', tr('scripts.check.save_anyway' if save_anyway else 'scripts.check.proceed'))
            alert.set_response_appearance('continue', Adw.ResponseAppearance.DESTRUCTIVE)
            alert.connect('response', lambda _dialog, response: proceed() if response == 'continue' else None)
        alert.present(parent or app.window)

    def findings_message(findings):
        return tr('scripts.check.full') + '\n' + '\n'.join(
            tr('scripts.check.capability', capability=capability, line=line) for capability, line in findings)

    def render():
        for row in state['rows']: listing.remove(row)
        state['rows'] = []
        for record in state['records']:
            row = Adw.ActionRow(title=record['name'], subtitle=record['trigger'], activatable=True)
            row.set_use_markup(False)
            toggle = Gtk.Switch(active=record['enabled'], valign=Gtk.Align.CENTER)
            def enabled(widget, _param, item=record):
                requested = widget.get_active()
                if requested == item['enabled']: return
                def apply():
                    old = item['enabled']
                    try:
                        item['enabled'] = requested
                        persist(); render()
                    except Exception as error:
                        item['enabled'] = old; render()
                        report('scripts.check.title', check_message(error))
                if not requested:
                    apply(); return
                widget.set_active(False)
                try:
                    findings = store.inspect(item['source'], item['independent'])
                except ValueError as error:
                    report('scripts.check.title', check_message(error)); return
                if findings:
                    body = findings_message(findings)
                    if app.values['ScriptMode'] != 'Full':
                        report('scripts.check.title', body + '\n' + tr('scripts.check.switch_full'))
                    else:
                        report('scripts.check.title', body, proceed=apply)
                else:
                    apply()
            toggle.connect('notify::active', enabled)
            row.add_suffix(toggle)
            button = Gtk.Button(icon_name='document-edit-symbolic', valign=Gtk.Align.CENTER)
            button.connect('clicked', lambda *_args, item=record: edit(item))
            row.add_suffix(button)
            row.connect('activated', lambda _row, item=record: edit(item))
            listing.add(row)
            state['rows'].append(row)

    def edit(record=None):
        dialog = Adw.Window(title=tr('scripts.edit'), transient_for=app.window,
                            modal=True, destroy_with_parent=True, default_width=760, default_height=720)
        toolbar = Adw.ToolbarView()
        toolbar.add_top_bar(Adw.HeaderBar())
        form = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, margin_top=12, margin_bottom=16, margin_start=16, margin_end=16)
        fields = Adw.PreferencesGroup()
        name = Adw.EntryRow(title=tr('scripts.name'), text=record['name'] if record else '')
        trigger = Adw.EntryRow(title=tr('scripts.trigger'), text=record['trigger'] if record else '')
        independent = Adw.SwitchRow(title=tr('scripts.independent'), subtitle=tr('scripts.independent_hint'))
        independent.set_active(record['independent'] if record else False)
        fields.add(name); fields.add(trigger); fields.add(independent); form.append(fields)
        instructions = Adw.PreferencesGroup()
        explanation = Adw.ExpanderRow(title=tr('scripts.explanation'))
        help_text = Gtk.Label(label=tr('scripts.editor_hint'), wrap=True, xalign=0,
                              margin_start=12, margin_end=12, margin_top=8, margin_bottom=12)
        explanation.add_row(help_text)
        instructions.add(explanation); form.append(instructions)
        content = Gtk.Label(label=tr('scripts.content'), xalign=0)
        content.add_css_class('caption'); content.add_css_class('dim-label')
        form.append(content)
        try:
            import gi
            gi.require_version('GtkSource', '5')
            from gi.repository import GtkSource
            buffer = GtkSource.Buffer()
            buffer.set_language(GtkSource.LanguageManager.get_default().get_language('lua'))
            buffer.set_highlight_syntax(True)
            style = Adw.StyleManager.get_default()
            schemes = GtkSource.StyleSchemeManager.get_default()
            def update_style(*_):
                scheme = schemes.get_scheme('Adwaita-dark' if style.get_dark() else 'Adwaita')
                if scheme: buffer.set_style_scheme(scheme)
            update_style()
            style_handler = style.connect('notify::dark', update_style)
            dialog.connect('close-request', lambda *_: style.disconnect(style_handler))
            editor = GtkSource.View(buffer=buffer, show_line_numbers=True, auto_indent=True,
                                    insert_spaces_instead_of_tabs=True, tab_width=2,
                                    monospace=True, left_margin=8, top_margin=8)
        except (ImportError, ValueError):
            editor = Gtk.TextView(monospace=True, wrap_mode=Gtk.WrapMode.NONE, left_margin=8, top_margin=8)
        editor.get_buffer().set_text(record['source'] if record else store.new_record()['source'])
        scroll = Gtk.ScrolledWindow(vexpand=True, min_content_height=240)
        scroll.set_child(editor); form.append(scroll)
        status = Gtk.Label(xalign=0, wrap=True); form.append(status)
        actions = Gtk.Box(spacing=8, halign=Gtk.Align.END)
        remove = Gtk.Button(label=tr('scripts.delete'), sensitive=record is not None)
        remove.add_css_class('destructive-action')
        save = Gtk.Button(label=tr('scripts.save')); save.add_css_class('suggested-action')
        actions.append(remove); actions.append(save); form.append(actions)
        form_scroll = Gtk.ScrolledWindow(vexpand=True, hscrollbar_policy=Gtk.PolicyType.NEVER)
        form_scroll.set_child(form)
        toolbar.set_content(form_scroll); dialog.set_content(toolbar)
        def save_record(*_):
            before = list(state['records'])
            try:
                value = dict(record) if record else store.new_record()
                buffer = editor.get_buffer()
                value.update(name=name.get_text().strip(), trigger=trigger.get_text().strip(), independent=independent.get_active(),
                             source=buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True))
                findings = store.inspect(value['source'], value['independent'])
                def apply():
                    try:
                        state['records'] = [value if r is record else r for r in before] if record else [*before, value]
                        persist(); render(); dialog.close()
                    except Exception as error:
                        state['records'] = before
                        status.set_text(check_message(error))
                if findings:
                    body = findings_message(findings) + '\n' + tr('scripts.check.save_note')
                    if value['enabled'] and app.values['ScriptMode'] != 'Full':
                        value['enabled'] = False
                        body += '\n' + tr('scripts.check.save_disabled')
                    report('scripts.check.title', body, parent=dialog, proceed=apply, save_anyway=True)
                else:
                    apply()
            except Exception as error:
                state['records'] = before
                status.set_text(check_message(error))
        def delete_record(*_):
            before = list(state['records'])
            try:
                state['records'] = [r for r in before if r is not record]
                persist(); render(); dialog.close()
            except Exception as error:
                state['records'] = before
                status.set_text(check_message(error))
        save.connect('clicked', save_record); remove.connect('clicked', delete_record)
        dialog.present()
    app.action(listing, 'scripts.add', lambda *_: edit())
    examples = app.group('scripts', 'scripts.examples', tr('scripts.example_hint'))
    def add_example(kind):
        before = list(state['records'])
        try:
            trigger, independent = store.EXAMPLES[kind]
            record = store.new_record(tr('scripts.' + kind), trigger, store.example(kind))
            record['independent'] = independent
            state['records'].append(record)
            persist(); render()
        except Exception as error:
            state['records'] = before
            app.failure(error)
    for kind in store.EXAMPLES:
        app.action(examples, 'scripts.' + kind, lambda *_args, k=kind: add_example(k))
    try:
        state['records'] = store.load(config.config_home())
        render()
    except Exception as error:
        listing.set_sensitive(False); examples.set_sensitive(False)
        app.toast(tr('scripts.error'))
    if app.load_error:
        app.pages['scripts'].set_sensitive(False)
