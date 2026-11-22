# SPDX-License-Identifier: GPL-3.0-or-later
"""Searchable, paged learning editor. Personal contents never enter diagnostics."""
from datetime import datetime
from pathlib import Path
import subprocess
import threading
import time


def show(app, store, config, Adw, Gtk, GLib, tr):
    dialog = Adw.Dialog(title=tr('learning.title'), content_width=720, content_height=620)
    toolbar = Adw.ToolbarView()
    toolbar.add_top_bar(Adw.HeaderBar())
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10, margin_start=16,
                  margin_end=16, margin_top=12, margin_bottom=16)
    toolbar.set_content(box)
    dialog.set_child(toolbar)
    search = Gtk.SearchEntry(placeholder_text=tr('learning.search'))
    box.append(search)
    info = Gtk.Label(xalign=0, wrap=True)
    box.append(info)
    scroll = Gtk.ScrolledWindow(vexpand=True, hscrollbar_policy=Gtk.PolicyType.NEVER)
    listing = Gtk.ListBox(selection_mode=Gtk.SelectionMode.NONE)
    listing.add_css_class('boxed-list')
    scroll.set_child(listing)
    box.append(scroll)
    navigation = Gtk.Box(spacing=8)
    previous = Gtk.Button(label=tr('learning.previous'))
    following = Gtk.Button(label=tr('learning.next'))
    refresh = Gtk.Button(label=tr('learning.refresh'))
    for button in (previous, following, refresh): navigation.append(button)
    box.append(navigation)
    state = {'records': [], 'page': 0, 'busy': False, 'closed': False}
    dialog.connect('closed', lambda *_: state.update(closed=True))

    def failure(error):
        key = str(error)
        info.set_text(tr(key if key.startswith('learning.') else 'learning.error.data'))

    def render(*_):
        child = listing.get_first_child()
        while child:
            following_child = child.get_next_sibling()
            listing.remove(child)
            child = following_child
        query = search.get_text().strip().casefold()
        records = [r for r in state['records'] if query in r.text.casefold() or query in r.reading]
        pages = max(1, (len(records) + 49) // 50)
        state['page'] = min(state['page'], pages - 1)
        info.set_text(tr('learning.summary', total=len(state['records']), matched=len(records),
                         page=state['page']+1, pages=pages))
        for record in records[state['page']*50:(state['page']+1)*50]:
            try: date = datetime.fromtimestamp(record.last).strftime('%Y-%m-%d %H:%M')
            except (ValueError, OverflowError, OSError): date = '—'
            row = Adw.ActionRow(title=record.text, subtitle=tr('learning.details',
                reading=record.reading, weight=record.weight(), count=record.count, date=date), activatable=True)
            row.set_use_markup(False)
            row.set_subtitle_lines(0)
            row.add_suffix(Gtk.Image.new_from_icon_name('document-edit-symbolic'))
            row.connect('activated', lambda _, r=record: edit(r))
            listing.append(row)
        previous.set_sensitive(state['page'] > 0 and not state['busy'])
        following.set_sensitive(state['page']+1 < pages and not state['busy'])

    def reload(*_):
        try:
            state['records'] = store.load(config.data_home())
            render()
        except Exception as error: failure(error)

    def apply(record, reading, text, count, child):
        if state['busy']: return
        try: request = store.submit(config.data_home(), config.atomic, record, reading, text, count)
        except Exception as error:
            failure(error)
            child.close()
            return
        child.close()
        state['busy'] = True
        listing.set_sensitive(False)
        refresh.set_sensitive(False)
        previous.set_sensitive(False)
        following.set_sensitive(False)
        info.set_text(tr('learning.applying'))

        def worker():
            # The helper only handles pending files; no personal text in argv.
            helper = 'cowtype'
            source_helper = Path(__file__).resolve().parents[2] / 'build/native/rust-target/release/cowtype'
            if source_helper.is_file(): helper = str(source_helper)
            try:
                subprocess.run([helper, '--apply-learning-edits'], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=5, check=False)
            except (OSError, subprocess.TimeoutExpired): pass
            status = None
            deadline = time.monotonic()+5
            try:
                while status is None and time.monotonic() < deadline:
                    status = store.result(config.data_home(), request)
                    if status is None: time.sleep(0.05)
            except Exception: status = 'learning.error.data'

            def finished():
                if state['closed']: return False
                state['busy'] = False
                listing.set_sensitive(True)
                refresh.set_sensitive(True)
                reload()
                info.set_text(tr('learning.applied' if status == 'applied' else status or 'learning.pending'))
                if status == 'applied': app.toast(tr('learning.applied'))
                return False
            GLib.idle_add(finished)
        threading.Thread(target=worker, daemon=True).start()

    def edit(record):
        if state['busy']: return
        child = Adw.Dialog(title=tr('learning.edit'), content_width=520)
        form = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12, margin_start=18,
                       margin_end=18, margin_top=18, margin_bottom=18)
        group = Adw.PreferencesGroup(description=tr('learning.edit_hint'))
        reading = Adw.EntryRow(title=tr('learning.reading'), text=record.reading)
        text = Adw.EntryRow(title=tr('learning.text'), text=record.text)
        weight = Adw.SpinRow.new_with_range(1, 0xffffffff, 1)
        weight.set_title(tr('learning.weight'))
        weight.set_value(max(1, record.weight()))
        for row in (reading, text, weight): group.add(row)
        form.append(group)
        buttons = Gtk.Box(spacing=8, halign=Gtk.Align.END)
        cancel = Gtk.Button(label=tr('action.cancel'))
        delete = Gtk.Button(label=tr('action.delete'))
        delete.add_css_class('destructive-action')
        save = Gtk.Button(label=tr('learning.save'))
        save.add_css_class('suggested-action')
        cancel.connect('clicked', lambda *_: child.close())
        save.connect('clicked', lambda *_: apply(record, reading.get_text().strip(), text.get_text(), int(weight.get_value()), child))
        delete.connect('clicked', lambda *_: apply(record, record.reading, record.text, 0, child))
        for button in (delete, cancel, save): buttons.append(button)
        form.append(buttons)
        child.set_child(form)
        child.present(app.window)

    def page(delta):
        state['page'] = max(0, state['page']+delta)
        render()
    previous.connect('clicked', lambda *_: page(-1))
    following.connect('clicked', lambda *_: page(1))
    refresh.connect('clicked', reload)
    search.connect('search-changed', lambda *_: (state.update(page=0), render()))
    reload()
    dialog.present(app.window)
    return dialog
