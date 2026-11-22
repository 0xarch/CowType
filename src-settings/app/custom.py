# SPDX-License-Identifier: GPL-3.0-or-later
"""Autosaved explicit-shortcut editor; no content in diagnostics or process argv."""

def show(app, store, config, Adw, Gtk, GLib, tr):
    dialog = Adw.Dialog(title=tr('custom.title'), content_width=720, content_height=640)
    toolbar = Adw.ToolbarView()
    toolbar.add_top_bar(Adw.HeaderBar())
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, margin_start=16,
                  margin_end=16, margin_top=12, margin_bottom=16)
    toolbar.set_content(box)
    dialog.set_child(toolbar)
    hint = Gtk.Label(label=tr('custom.hint'), wrap=True, xalign=0)
    box.append(hint)
    search = Gtk.SearchEntry(placeholder_text=tr('custom.search'))
    box.append(search)
    info = Gtk.Label(xalign=0, wrap=True)
    box.append(info)
    scroll = Gtk.ScrolledWindow(vexpand=True, hscrollbar_policy=Gtk.PolicyType.NEVER)
    listing = Gtk.ListBox(selection_mode=Gtk.SelectionMode.NONE)
    listing.add_css_class('boxed-list')
    scroll.set_child(listing)
    box.append(scroll)
    buttons = Gtk.Box(spacing=8)
    add = Gtk.Button(label=tr('custom.add'))
    add.add_css_class('suggested-action')
    previous = Gtk.Button(label=tr('learning.previous'))
    following = Gtk.Button(label=tr('learning.next'))
    refresh = Gtk.Button(label=tr('learning.refresh'))
    for button in (add, previous, following, refresh): buttons.append(button)
    box.append(buttons)
    state = {'records': [], 'page': 0, 'closed': False}
    dialog.connect('closed', lambda *_: state.update(closed=True))

    def message(error):
        key = str(error)
        return tr(key if key in ('custom.error.data', 'custom.error.conflict', 'custom.error.value', 'custom.error.limit') else 'custom.error.data')

    def render(*_):
        child = listing.get_first_child()
        while child:
            next_child = child.get_next_sibling()
            listing.remove(child)
            child = next_child
        query = search.get_text().strip().casefold()
        records = [r for r in state['records'] if query in r.trigger or query in r.text.casefold()]
        records.sort(key=lambda r: (r.trigger, -r.weight))
        pages = max(1, (len(records)+49)//50)
        state['page'] = min(state['page'], pages-1)
        info.set_text(tr('learning.summary', total=len(state['records']), matched=len(records), page=state['page']+1, pages=pages))
        for record in records[state['page']*50:(state['page']+1)*50]:
            preview = record.text.replace('\n', ' ↵ ').replace('\t', ' ⇥ ')
            row = Adw.ActionRow(title=record.trigger, subtitle=tr('custom.details', weight=record.weight, text=preview[:160]), activatable=True)
            row.set_use_markup(False)
            row.set_subtitle_lines(2)
            row.add_suffix(Gtk.Image.new_from_icon_name('document-edit-symbolic'))
            row.connect('activated', lambda _, r=record: edit(r))
            listing.append(row)
        previous.set_sensitive(state['page'] > 0)
        following.set_sensitive(state['page']+1 < pages)

    def reload(*_):
        if state['closed']: return
        try:
            state['records'] = store.load(config.data_home())
            render()
            add.set_sensitive(True)
        except Exception as error:
            info.set_text(message(error))
            add.set_sensitive(False)

    def edit(record=None):
        child = Adw.Dialog(title=tr('custom.edit' if record else 'custom.add'), content_width=560, content_height=510)
        view = Adw.ToolbarView()
        view.add_top_bar(Adw.HeaderBar())
        form = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, margin_start=18,
                       margin_end=18, margin_top=12, margin_bottom=18)
        group = Adw.PreferencesGroup(description=tr('custom.weight_hint'))
        trigger = Adw.EntryRow(title=tr('custom.trigger'), text=record.trigger if record else '')
        weight = Adw.SpinRow.new_with_range(1, 1_000_000, 1)
        weight.set_title(tr('custom.weight'))
        weight.set_value(record.weight if record else 100)
        group.add(trigger)
        group.add(weight)
        form.append(group)
        form.append(Gtk.Label(label=tr('custom.content'), xalign=0))
        text = Gtk.TextView(wrap_mode=Gtk.WrapMode.WORD_CHAR, top_margin=8, bottom_margin=8,
                            left_margin=8, right_margin=8, accepts_tab=True)
        text.get_buffer().set_text(record.text if record else '')
        scroll_text = Gtk.ScrolledWindow(vexpand=True, min_content_height=120)
        scroll_text.set_child(text)
        form.append(scroll_text)
        status = Gtk.Label(label=tr('custom.autosave'), wrap=True, xalign=0)
        form.append(status)
        actions = Gtk.Box(spacing=8, halign=Gtk.Align.END)
        delete = Gtk.Button(label=tr('action.delete'), sensitive=record is not None)
        delete.add_css_class('destructive-action')
        close = Gtk.Button(label=tr('custom.close'))
        actions.append(delete)
        actions.append(close)
        form.append(actions)
        view.set_content(form)
        child.set_child(view)
        editing = {'record': record, 'timer': None, 'deleted': False, 'dirty': False}

        def save():
            editing['timer'] = None
            if not editing['dirty'] or editing['deleted']: return False
            buffer = text.get_buffer()
            new = store.Record(trigger.get_text().strip().lower(), buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True), int(weight.get_value()))
            try:
                if new != editing['record']:
                    store.change(config.data_home(), config.atomic, editing['record'], new)
                editing.update(record=new, dirty=False)
                delete.set_sensitive(True)
                status.set_text(tr('custom.saved'))
                reload()
            except Exception as error:
                status.set_text(message(error))
            return False

        def changed(*_):
            editing['dirty'] = True
            status.set_text(tr('custom.autosave'))
            if editing['timer']: GLib.source_remove(editing['timer'])
            editing['timer'] = GLib.timeout_add(350, save)

        def finish(*_):
            if editing['timer']: GLib.source_remove(editing['timer'])
            save()
            if editing['dirty'] and not editing['deleted']: app.toast(status.get_text())

        def remove(*_):
            try:
                store.change(config.data_home(), config.atomic, editing['record'], None)
                editing['deleted'] = True
                reload()
                child.close()
            except Exception as error: status.set_text(message(error))

        trigger.connect('changed', changed)
        weight.connect('notify::value', changed)
        text.get_buffer().connect('changed', changed)
        delete.connect('clicked', remove)
        close.connect('clicked', lambda *_: child.close())
        child.connect('closed', finish)
        child.present(app.window)
        return child

    def page(delta):
        state['page'] = max(0, state['page']+delta)
        render()
    add.connect('clicked', lambda *_: edit())
    previous.connect('clicked', lambda *_: page(-1))
    following.connect('clicked', lambda *_: page(1))
    refresh.connect('clicked', reload)
    search.connect('search-changed', lambda *_: (state.update(page=0), render()))
    reload()
    dialog.present(app.window)
    return dialog
