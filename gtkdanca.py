#!/usr/bin/python3
# -*- coding: utf-8 -*-

from gi.repository import Gtk, Gio, GObject, GLib
import bluezutils
import dbus
import dbus.mainloop.glib
import dbus.service
import os
import pickle
import sys

RFCOMM_UUID = '00001101-0000-1000-8000-00805f9b34fb'
AGENT_INTERFACE = 'org.bluez.Agent1'
PROFILE_INTERFACE = 'org.bluez.Profile1'

def known_address(address):
    if address.startswith('20:13:09'):
      return True
    if address.startswith('00:14:02'):
      return True
    return False


class Profile(dbus.service.Object):
    @dbus.service.method(PROFILE_INTERFACE,
                            in_signature="oha{sv}", out_signature="")
    def NewConnection(self, path, fd, properties):
        print('Dancer profiler received new connection, fd %s' % fd)
        BluetoothDevice.INSTANCES_BY_PATH[path].set_fd_and_path(fd, path)


class Agent(dbus.service.Object):
    exit_on_release = True

    def set_exit_on_release(self, exit_on_release):
        self.exit_on_release = exit_on_release

    @dbus.service.method(AGENT_INTERFACE,
                    in_signature="", out_signature="")
    def Release(self):
        print("Release")
        if self.exit_on_release:
            mainloop.quit()

    def __set_trusted(self, path):
        props = dbus.Interface(bus.get_object("org.bluez", path),
                        "org.freedesktop.DBus.Properties")
        props.Set("org.bluez.Device1", "Trusted", True)

    @dbus.service.method(AGENT_INTERFACE,
                    in_signature="os", out_signature="")
    def AuthorizeService(self, device, uuid):
        print("AuthorizeService (%s, %s)" % (device, uuid))
        raise Rejected("Connection rejected by user")

    @dbus.service.method(AGENT_INTERFACE,
                    in_signature="o", out_signature="s")
    def RequestPinCode(self, device):
        print("RequestPinCode (%s)" % (device))
        self.__set_trusted(device)
        return "0000"

    @dbus.service.method(AGENT_INTERFACE,
                    in_signature="o", out_signature="u")
    def RequestPasskey(self, device):
        print("RequestPasskey (%s)" % (device))
        self.__set_trusted(device)
        return dbus.UInt32(0)

    @dbus.service.method(AGENT_INTERFACE,
                    in_signature="ouq", out_signature="")
    def DisplayPasskey(self, device, passkey, entered):
        print("DisplayPasskey (%s, %06u entered %u)" %
                        (device, passkey, entered))

    @dbus.service.method(AGENT_INTERFACE,
                    in_signature="os", out_signature="")
    def DisplayPinCode(self, device, pincode):
        print("DisplayPinCode (%s, %s)" % (device, pincode))

    @dbus.service.method(AGENT_INTERFACE,
                    in_signature="ou", out_signature="")
    def RequestConfirmation(self, device, passkey):
        print("RequestConfirmation (%s, %06d)" % (device, passkey))
        self.__set_trusted(device)

    @dbus.service.method(AGENT_INTERFACE,
                    in_signature="o", out_signature="")
    def RequestAuthorization(self, device):
        print("RequestAuthorization (%s)" % (device))
        if not device.startswith("Dancarino"):
            raise Rejected("Pairing rejected")

    @dbus.service.method(AGENT_INTERFACE,
                    in_signature="", out_signature="")
    def Cancel(self):
        print("Cancel")

class Dancarino:
    def __init__(self, fd, bt_path):
        self.fd = fd
        self.bt_path = bt_path

    def __del__(self):
        os.close(self.fd)

    def __write(self, s):
        try:
          s = bytes(s, 'iso-8859-1')
          print('Writing %d bytes to to fd %d: %s' % (
            len(s),
            self.fd,
            ','.join(str(c) for c in s)
          ))
          os.write(self.fd, s)
        except:
          print('Error writing to fd %d, ignoring' % self.fd)

    def test_mode(self):
        self.__write('T')

    def reset(self):
        self.__write('R')

    def analog_write(self, wire, value):
        assert(wire in (0, 1, 2))
        assert(0 <= value <= 255)

        self.__write('A%c%c' % (chr(wire), chr(value)))

    def digital_write(self, wire, value):
        assert(wire in (0, 1, 2))
        value = 1 if value else 0

        self.__write('D%c%c' % (chr(wire), chr(value)))

    def toggle_led(self):
        self.__write(bytes('L'))

    def __calculate_fade_duration(self, duration_in_s):
        assert(0 <= duration_in_s <= 8.0)

        duration_in_ms = duration_in_s * 1000.
        step_in_ms = duration_in_ms / 100.

        # 0.30980392 is the angular coefficient for the line that goes from
        # (0, 1) to (255, 80).  80ms is the time spent in each step to
        # produce a 8000ms fade effect.  a param of 255 will fade for 8s, a
        # param of 127 will fade for roughly 4s, and so on.
        param = int((step_in_ms - 1.) / 0.30980392)
        if param > 255: return 255
        if param < 1: return 1
        return param

    def fade_in(self, wire, duration):
        assert(wire in (0, 1, 2))
        prev_duration = duration
        duration = self.__calculate_fade_duration(duration)
        print("Para um fade de %f segundos, param=%d" % (prev_duration, duration))
        self.__write('I%c%c' % (chr(wire), chr(duration)))

    def fade_out(self, wire, duration):
        assert(wire in (0, 1, 2))
        prev_duration = duration
        duration = self.__calculate_fade_duration(duration)
        print("Para um fade de %f segundos, param=%d" % (prev_duration, duration))
        self.__write('O%c%c' % (chr(wire), chr(duration)))

    def strobe(self, wire, times, delay_between_blink):
        assert(wire in (0, 1, 2))

        self.__write('S%c%c%c' % (chr(wire), chr(times), chr(delay_between_blink)))

    def set_number(self, number):
        assert(1 <= number <= 5)

        self.__write('N%c' % chr(number))

    def strobe(self, wire, intervalo, piscadas):
        assert(wire in (0, 1, 2))
        self.__write('S%c%c%c' % (chr(wire), chr(intervalo), chr(piscadas)))


class BluetoothDevice:
  DEVICES = {}
  INSTANCES_BY_PATH = {}
  ADAPTER = None
  WINDOW = None

  @staticmethod
  def get_instance_by_addr(addr):
    for instance in BluetoothDevice.INSTANCES_BY_PATH:
      if BluetoothDevice.DEVICES[instance].get('Address', None) == addr:
        return BluetoothDevice.INSTANCES_BY_PATH[instance]
    return None

  @staticmethod
  def find_dancer_by_id(id):
    id = str(id)
    for path, device in BluetoothDevice.INSTANCES_BY_PATH.items():
      if not device.dancarino:
        continue
      props = BluetoothDevice.DEVICES.get(path, None)
      if not props:
        continue
      if props['Name'].split(' ')[-1] == id:
        return device.dancarino
    return None

  @staticmethod
  def unpair(address):
    print('Unpairing device %s' % address)
    device = bluezutils.find_device(address)
    BluetoothDevice.ADAPTER.RemoveDevice(device.object_path)

  @staticmethod
  def start_discovery(window):
    BluetoothDevice.WINDOW = window

    bus.add_signal_receiver(BluetoothDevice.interfaces_added,
      dbus_interface = 'org.freedesktop.DBus.ObjectManager',
      signal_name = 'InterfacesAdded')

    bus.add_signal_receiver(BluetoothDevice.properties_changed,
      dbus_interface = 'org.freedesktop.DBus.Properties',
      signal_name = 'PropertiesChanged',
      arg0 = 'org.bluez.Device1',
      path_keyword = 'path')

    BluetoothDevice.ADAPTER = bluezutils.find_adapter(None)

    om = dbus.Interface(bus.get_object('org.bluez', '/'),
      'org.freedesktop.DBus.ObjectManager')
    for path, interfaces in om.GetManagedObjects().items():
      if 'org.bluez.Device1' in interfaces:
        BluetoothDevice.unpair(interfaces['org.bluez.Device1']['Address'])

    props = dbus.Interface(bus.get_object("org.bluez",
      BluetoothDevice.ADAPTER.object_path), "org.freedesktop.DBus.Properties")
    props.Set("org.bluez.Adapter1", "Powered", True)

    BluetoothDevice.ADAPTER.StartDiscovery()

  @staticmethod
  def interfaces_added(path, interfaces):
    props = interfaces.get('org.bluez.Device1', None)
    if not props:
      return
    print(path + ': added iface')
    if path in BluetoothDevice.DEVICES:
      BluetoothDevice.DEVICES[path].update(props)
    else:
      BluetoothDevice.DEVICES[path] = props
    if not path in BluetoothDevice.INSTANCES_BY_PATH:
      print('Creating bt device for path %s' % path)
      BluetoothDevice.INSTANCES_BY_PATH[path] = BluetoothDevice(props['Address'])
    BluetoothDevice.WINDOW.update()

  @staticmethod
  def properties_changed(interface, changed, invalidated, path):
    if interface != 'org.bluez.Device1':
      return

    if path in BluetoothDevice.DEVICES:
      BluetoothDevice.DEVICES[path].update(changed)

      if not changed.get('Connected', True):
        print('Lost connection to device %s, reconnecting' % path)
        BluetoothDevice.INSTANCES_BY_PATH[path].connect()
    else:
      BluetoothDevice.DEVICES[path] = changed
      addr = None
      if 'Address' in changed:
        addr = changed['Address']
      else:
        mo = dbus.Interface(bus.get_object('org.bluez', '/'),
          'org.freedesktop.DBus.ObjectManager').GetManagedObjects()
        obj = mo.get(path, None)
        if obj:
          props = obj.get('org.bluez.Device1', None)
          if props:
            addr = props['Address']
            BluetoothDevice.DEVICES[path].update(props)

      if addr is not None:
        BluetoothDevice.INSTANCES_BY_PATH[path] = BluetoothDevice(addr)
    BluetoothDevice.WINDOW.update()

  def __init__(self, address):
    self.fd = -1
    self.connecting = False
    self.dancarino = None
    self.address = address
    self.device = bluezutils.find_device(address)
    if known_address(address):
      self.__pair()

  def __pair(self):
    def error_handler(error):
      err_name = error.get_dbus_name()
      if err_name == "org.freedesktop.DBus.Error.NoReply" and self.device:
          self.device.CancelPairing()
      elif err_name == "org.bluez.Error.AlreadyExists":
          print("Already paired with device %s, trying to connect" % self.address)
          self.connect()
      else:
          print("Creating device failed, trying again: %s" % (error))
          try:
            self.device.CancelPairing()
          except:
            pass
          GLib.timeout_add_seconds(1, self.__pair)

    def reply_handler():
      dev_path = self.device.object_path
      print("Paired with device %s, path %s" % (self.device, dev_path))
      props = dbus.Interface(bus.get_object('org.bluez', dev_path),
        'org.freedesktop.DBus.Properties')
      props.Set('org.bluez.Device1', 'Trusted', True)
      self.connect()

    print('Trying to pair with address %s' % self.address)
    self.device.Pair(reply_handler=reply_handler,
      error_handler=error_handler, timeout=75000)

  def set_fd_and_path(self, fd, path):
    self.fd = fd.take()
    self.dancarino = Dancarino(self.fd, path)
    self.connecting = False
    BluetoothDevice.WINDOW.update()

  def connect(self):
    if self.connecting or not known_address(self.address):
      return

    dev_path = self.device.object_path

    print("Connecting to device %s, path %s" % (self.address, dev_path))

    def reply_handler(*args):
      print('Inside connect reply handler: %s' % str(args))

    def error_handler(*args):
      print('Inside connect error handler: %s' % str(args))
      if 'refused' in args[0].get_dbus_name():
        BluetoothDevice.unpair(self.address)
        GLib.timeout_add_seconds(1, self.__pair)
      else:
        GLib.timeout_add_seconds(1, self.connect)

    try:
      dev = dbus.Interface(bus.get_object('org.bluez', dev_path),
        'org.bluez.Device1')
      self.connecting = True
      dev.ConnectProfile(RFCOMM_UUID, reply_handler=reply_handler,
        error_handler=error_handler, timeout=75000)
    except dbus.exceptions.DBusException as e:
      self.fd = -1
      BluetoothDevice.WINDOW.update()

class WaitAction(GObject.GObject):
  attrs = (('time', 'Tempo (s)'),)

  def __init__(self, time):
    GObject.GObject.__init__(self)
    self.time = float(time)

  def as_string_for_list_view(self):
    return 'Espera %s' % self.time

  def serialize(self):
    return {'action':'WaitAction', 'attrs': [self.time]}

  def perform(self, dancarino):
    pass

class TurnOnAction(GObject.GObject):
  attrs = (('fio', 'Fio (0 ou 1; 2 liga ambos)'),)

  def __init__(self, fio):
    GObject.GObject.__init__(self)
    self.fio = int(fio)

  def as_string_for_list_view(self):
    if self.fio == 2:
      return 'Liga ambos'
    return 'Liga %s' % self.fio

  def serialize(self):
    return {'action': 'TurnOnAction', 'attrs': [self.fio]}

  def perform(self, dancarino):
    dancarino.digital_write(self.fio, 1)

class TurnOffAction(GObject.GObject):
  attrs = (('fio', 'Fio (0 ou 1; 2 desliga ambos)'),)

  def __init__(self, fio):
    GObject.GObject.__init__(self)
    self.fio = int(fio)

  def as_string_for_list_view(self):
    if self.fio == 2:
      return 'Desliga ambos'
    return 'Desliga %s' % self.fio

  def serialize(self):
    return {'action': 'TurnOffAction', 'attrs': [self.fio]}

  def perform(self, dancarino):
    dancarino.digital_write(self.fio, 0)

class StrobeAction(GObject.GObject):
  attrs = (('fio', 'Fio (0 ou 1; 2 ambos)'),
    ('intervalo', 'Intervalo entre piscada (ms)'),
    ('piscadas', 'Piscadas'))

  def __init__(self, fio, intervalo, piscadas):
    GObject.GObject.__init__(self)
    self.fio = int(fio)
    self.intervalo = float(intervalo)
    self.piscadas = int(piscadas)

  def as_string_for_list_view(self):
    if self.fio == 2:
      return 'Strobe ambos (%sx, %ss entre piscada)' % (
        self.piscadas,
        self.intervalo
      )

    return 'Strobe fio %d (%sx, %ss entre piscada)' % (
      self.fio,
      self.piscadas,
      self.intervalo
    )

  def serialize(self):
    return {
      'action': 'StrobeAction',
      'attrs': [
        self.fio,
        self.intervalo,
        self.piscadas
      ]
    }

  def perform(self, dancarino):
    dancarino.strobe(self.fio, self.intervalo, self.piscadas)

class FadeInAction(GObject.GObject):
  attrs = (('fio', 'Fio (0 ou 1; 2 ambos)'), ('duration', 'Duração (s)'))

  def __init__(self, fio, duration):
    GObject.GObject.__init__(self)
    self.fio = int(fio)
    self.duration = float(duration)

  def as_string_for_list_view(self):
    if self.fio == 2:
      return 'Fade in ambos (%ss)' % (self.duration)
    return 'Fade in %s (%ss)' % (self.fio, self.duration)

  def serialize(self):
    return {'action': 'FadeInAction', 'attrs': [self.fio, self.duration]}

  def perform(self, dancarino):
    dancarino.fade_in(self.fio, self.duration)

class FadeOutAction(GObject.GObject):
  attrs = (('fio', 'Fio (0 ou 1; 2 ambos)'), ('duration', 'Duração (s)'))

  def __init__(self, fio, duration):
    GObject.GObject.__init__(self)
    self.fio = int(fio)
    self.duration = float(duration)

  def as_string_for_list_view(self):
    if self.fio == 2:
      return 'Fade out ambos (%ss)' % (self.duration)
    return 'Fade out %s (%ss)' % (self.fio, self.duration)

  def serialize(self):
    return {'action': 'FadeOutAction; 2 ambos', 'attrs': [self.fio, self.duration]}

  def perform(self, dancarino):
    dancarino.fade_out(self.fio, self.duration)


class Actions(GObject.GObject):
  def __init__(self, actions=[]):
    GObject.GObject.__init__(self)
    self.actions = actions

  def as_string_for_list_view(self):
    if not self.actions:
      return '« Vazio »'

    return '\n'.join(
      ' • %s' % action.as_string_for_list_view() for action in self.actions)

  def serialize(self):
    return [action.serialize() for action in self.actions]

  @staticmethod
  def deserialize(serialized):
    actions = []

    for serialized_action in serialized:
      action = globals()[serialized_action['action']]
      if 'attrs' in serialized_action:
        action = action(*serialized_action['attrs'])
      else:
        action = action()

      actions.append(action)

    return Actions(actions)


class ActionConstructDialog(Gtk.Dialog):
  def __init__(self, parent, attrs, values={}):
    if not values:
      Gtk.Dialog.__init__(self, 'Parâmetros', parent, 0,
        (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
        Gtk.STOCK_SAVE, Gtk.ResponseType.OK))
    else:
      Gtk.Dialog.__init__(self, 'Editar Parâmetros', parent, 0,
        (Gtk.STOCK_DELETE, Gtk.ResponseType.REJECT,
        Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
        Gtk.STOCK_SAVE, Gtk.ResponseType.OK))

    box = Gtk.ListBox()
    box.set_selection_mode(Gtk.SelectionMode.NONE)
    self.get_content_area().add(box)

    first = True
    self.values = {}
    for name, text in attrs:
      row = Gtk.ListBoxRow()
      hbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=50)
      row.add(hbox)
      label = Gtk.Label(text, xalign=0)

      self.values[name] = None

      entry = Gtk.Entry()
      entry.action_param_name = name
      entry.connect('changed', self.changed_entry)
      entry.set_text(values.get(name, '0'))
      if first:
        entry.grab_focus()
        first = False

      hbox.pack_start(label, True, True, 0)
      hbox.pack_start(entry, False, True, 0)

      box.add(row)

    self.show_all()

  def changed_entry(self, entry):
    self.values[entry.action_param_name] = entry.get_text()


class ActionsEditor(Gtk.Dialog):
  def __init__(self, parent, tempo, dancer, actions):
    Gtk.Dialog.__init__(self,
      'Tempo %d, dançarino %d' % (tempo, dancer), parent, 0,
      (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
      Gtk.STOCK_SAVE, Gtk.ResponseType.OK))
    self.props.default_height=500

    hb = Gtk.HeaderBar()
    hb.props.title = self.props.title
    self.set_titlebar(hb)

    def add_action_button(label, action_type):
      button = Gtk.Button(label)
      button.connect('clicked', lambda a: self.add_action(action_type))
      hb.pack_start(button)

    add_action_button('Espera', WaitAction)
    add_action_button('Desl.', TurnOffAction)
    add_action_button('Liga', TurnOnAction)
    add_action_button('Fade in', FadeInAction)
    add_action_button('Fade out', FadeOutAction)
    add_action_button('Strobe', StrobeAction)

    self.store = Gtk.ListStore(GObject.GObject)
    self.list = Gtk.TreeView(self.store)
    self.list.set_reorderable(True)

    renderer = Gtk.CellRendererText()
    column = Gtk.TreeViewColumn('Ação', renderer)
    self.list.append_column(column)
    column.set_cell_data_func(renderer, self.get_action)

    scrolled = Gtk.ScrolledWindow()
    scrolled.set_hexpand(True)
    scrolled.set_vexpand(True)

    self.get_content_area().add(scrolled)
    scrolled.add(self.list)

    self.list.connect('row-activated', self.edit_action)

    for action in actions.actions:
      self.store.append([action])

    self.show_all()

  def edit_action(self, list, path, column):
    action = self.store[path][0]
    if not action.attrs:
      return

    values = {}
    serialized = action.serialize()
    for attr, value in zip(action.attrs, serialized['attrs']):
      values[attr[0]] = str(value)

    dialog = ActionConstructDialog(self, action.attrs, values)
    response = dialog.run()
    if response == Gtk.ResponseType.OK:
      values = [dialog.values[name] for name, _ in action.attrs]
      action = action.__class__(*values)
      self.store[path] = [action]
    elif response == Gtk.ResponseType.REJECT:
      self.store.remove(self.store.get_iter(path))

    dialog.destroy()

  def add_action(self, action_type, *args):
    attrs = action_type.attrs
    action = None
    if not attrs:
      action = action_type()
    else:
      dialog = ActionConstructDialog(self, attrs)
      response = dialog.run()
      if response == Gtk.ResponseType.OK:
        values = [dialog.values[name] for name, _ in attrs]
        action = action_type(*values)

      dialog.destroy()

    if not action is None:
      self.store.append([action])

  def get_action(self, column, cell, model, iter, data):
    cell.set_property('text',
                  self.store.get_value(iter, 0).as_string_for_list_view())


class Maestro:
  def __init__(self, actions, dancer):
    self.actions = actions
    self.current_action = 0 if self.actions else -1
    self.dancer = BluetoothDevice.find_dancer_by_id(dancer)

    print('Creating Maestro for dancer %s' % dancer)

  def next(self):
    if self.current_action < 0:
      return

    try:
      action = self.actions[self.current_action]
    except IndexError:
      return

    self.current_action += 1

    if isinstance(action, WaitAction):
      print("[dancer %s] Waiting: %ss" % (self.dancer, action.time))
      GLib.timeout_add(int(float(action.time) * 1000), self.next)
    else:
      if self.dancer:
        action.perform(self.dancer)
      else:
        print('[dancer %s] Nao consegui achar dancarino com esse ID, ignorando' % self.dancer)
      print('[dancer %s] Performing: %s' % (self.dancer, action.as_string_for_list_view()))
      GLib.idle_add(self.next)


class RenameBluetoothDevice(Gtk.Dialog):
  def __init__(self, parent, current_name):
    Gtk.Dialog.__init__(self, 'Mudar ID do dançarino', parent, 0,
      ('Conectar', Gtk.ResponseType.APPLY,
      Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
      Gtk.STOCK_SAVE, Gtk.ResponseType.OK))

    number = current_name.split(' ')[-1]

    box = Gtk.ListBox()
    box.set_selection_mode(Gtk.SelectionMode.NONE)
    self.get_content_area().add(box)

    row = Gtk.ListBoxRow()
    hbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=50)
    row.add(hbox)
    label = Gtk.Label('Número (1 a 5)', xalign=0)

    self.entry = entry = Gtk.Entry()
    entry.set_text(number)
    entry.grab_focus()

    hbox.pack_start(label, True, True, 0)
    hbox.pack_start(entry, False, True, 0)

    box.add(row)

    self.show_all()


class BluetoothWindow(Gtk.Window):
  def __init__(self):
    Gtk.Window.__init__(self, title='Dispositivos Bluetooth')
    self.props.default_width=500
    self.props.default_height=500

    hb = Gtk.HeaderBar()
    hb.props.show_close_button = False
    hb.props.title = self.props.title
    self.set_titlebar(hb)

    self.store = Gtk.ListStore(str, str, str, str)
    self.list = Gtk.TreeView(self.store)

    renderer = Gtk.CellRendererText()
    column = Gtk.TreeViewColumn('Endereço', renderer, text=0)
    self.list.append_column(column)

    renderer = Gtk.CellRendererText()
    column = Gtk.TreeViewColumn('Estado', renderer, text=1)
    self.list.append_column(column)

    renderer = Gtk.CellRendererText()
    column = Gtk.TreeViewColumn('RSSI', renderer, text=2)
    self.list.append_column(column)

    renderer = Gtk.CellRendererText()
    column = Gtk.TreeViewColumn('Dançarino', renderer, text=3)
    self.list.append_column(column)

    scrolled = Gtk.ScrolledWindow()
    scrolled.set_hexpand(True)
    scrolled.set_vexpand(True)
    self.add(scrolled)
    scrolled.add(self.list)

    self.list.connect('row-activated', self.rename_bluetooth)

  def rename_bluetooth(self, list, path, column):
    addr = self.store[path][0]
    curr_name = self.store[path][3]

    dialog = RenameBluetoothDevice(self, curr_name)
    response = dialog.run()
    if response == Gtk.ResponseType.OK:
      try:
        number = int(dialog.entry.get_text())
        if number < 1 or number > 5:
          raise ValueError
      except ValueError:
        dialog.destroy()
        rename_bluetooth(self, list, path, column)

      instance = BluetoothDevice.get_instance_by_addr(addr)
      if instance and instance.dancarino:
        instance.dancarino.set_number(number)
      BluetoothDevice.unpair(addr)
    elif response == Gtk.ResponseType.APPLY:
      instance = BluetoothDevice.get_instance_by_addr(addr)
      if instance and instance.fd < 0:
        instance.connect()
        self.update()


    dialog.destroy()

  def update(self):
    self.store.clear()

    for path in BluetoothDevice.INSTANCES_BY_PATH:
      device_props = BluetoothDevice.DEVICES[path]
      instance = BluetoothDevice.INSTANCES_BY_PATH[path]

      if device_props.get('Connected', False):
        if instance.fd < 0:
          state = 'Conectando'
        else:
          state = 'Conectado (fd %d)' % instance.fd
      else:
        state = 'Desconectado'

      self.store.append([
        device_props.get('Address', 'N/D'),
        state,
        str(device_props.get('RSSI', 'N/D')),
        device_props.get('Name', 'N/D')
      ])


class MainWindow(Gtk.Window):
  def __init__(self):
    Gtk.Window.__init__(self, title='Controle de Iluminação Bluetooth')
    self.props.default_width=600
    self.props.default_height=700

    hb = Gtk.HeaderBar()
    hb.props.show_close_button = True
    hb.props.title = self.props.title
    self.set_titlebar(hb)

    button = Gtk.Button()
    icon = Gio.ThemedIcon(name='player_play')
    image = Gtk.Image.new_from_gicon(icon, Gtk.IconSize.BUTTON)
    button.connect('clicked', self.orchestrate)
    button.add(image)
    hb.pack_end(button)

    button = Gtk.Button()
    icon = Gio.ThemedIcon(name='add')
    image = Gtk.Image.new_from_gicon(icon, Gtk.IconSize.BUTTON)
    button.connect('clicked', self.add_tempo)
    button.add(image)
    hb.pack_start(button)

    button = Gtk.Button()
    icon = Gio.ThemedIcon(name='fileopen')
    image = Gtk.Image.new_from_gicon(icon, Gtk.IconSize.BUTTON)
    button.connect('clicked', self.open)
    button.add(image)
    hb.pack_start(button)

    button = Gtk.Button()
    icon = Gio.ThemedIcon(name='filesave')
    image = Gtk.Image.new_from_gicon(icon, Gtk.IconSize.BUTTON)
    button.connect('clicked', self.save)
    button.add(image)
    hb.pack_start(button)

    self.store = Gtk.ListStore(int, str,
      Actions.__gtype__, Actions.__gtype__, Actions.__gtype__,
      Actions.__gtype__, Actions.__gtype__)
    self.list = Gtk.TreeView(self.store)
    self.list.props.rules_hint = True

    renderer = Gtk.CellRendererText()
    column = Gtk.TreeViewColumn('Tempo', renderer, text=0)
    self.list.append_column(column)
    renderer = Gtk.CellRendererText()
    column = Gtk.TreeViewColumn('Intervalo', renderer, text=1)
    self.list.append_column(column)

    for dancer in range(5):
      renderer = Gtk.CellRendererText()
      column = Gtk.TreeViewColumn('Dançarino %d' % (dancer + 1),
        renderer)
      column.dancer = dancer

      self.list.append_column(column)
      column.set_cell_data_func(renderer, self.get_actions)

    scrolled = Gtk.ScrolledWindow()
    scrolled.set_hexpand(True)
    scrolled.set_vexpand(True)
    self.add(scrolled)
    scrolled.add(self.list)

    self.list.connect('row-activated', self.edit_actions)

    self.connect('delete-event', Gtk.main_quit)

    self.bluetooth_window = None

  def orchestrate(self, *args):
    start_time = 0

    for row in self.store:
      for dancer in range(5):
        actions = row[dancer + 2].actions
        if actions:
          def make_maestro(a, d):
            maestro = Maestro(a, d + 1)
            return lambda: maestro.next()

          GLib.timeout_add_seconds(start_time,
                        make_maestro(actions, dancer))

      start_time += 8


  def get_actions(self, column, cell, model, iter, data):
    column = column.dancer + 2
    cell.set_property('text',
                  self.store.get_value(iter, column).as_string_for_list_view())

  def add_tempo(self, *args):
    tempos = len(self.store)

    first_tempo = tempos * 8
    second_tempo = first_tempo + 8

    self.store.append([
      tempos + 1,
      '%ds - %ds' % (first_tempo, second_tempo),
      Actions(),
      Actions(),
      Actions(),
      Actions(),
      Actions()
    ])

  def edit_actions(self, list, path, column):
    dancer_data = self.store[path]
    tempo = dancer_data[0]
    try:
      actions = dancer_data[column.dancer + 2]
    except:
      return

    dialog = ActionsEditor(self, tempo, column.dancer + 2, actions)
    response = dialog.run()
    if response == Gtk.ResponseType.OK:
      actions.actions = [action[0] for action in dialog.store]

    dialog.destroy()

  def open(self, *args):
    dialog = Gtk.FileChooserDialog("Abrir", self,
        Gtk.FileChooserAction.OPEN,
        (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
         Gtk.STOCK_OPEN, Gtk.ResponseType.OK))

    response = dialog.run()
    if response == Gtk.ResponseType.OK:
      f = open(dialog.get_filename(), 'rb')
      pickled = f.read()
      f.close()

      self.store.clear()
      for tempo in pickle.loads(pickled):
        tempos = len(self.store)
        first_tempo = tempos * 8
        second_tempo = first_tempo + 8

        self.store.append([
          tempos + 1,
          '%ds - %ds' % (first_tempo, second_tempo),
          Actions.deserialize(tempo[0]),
          Actions.deserialize(tempo[1]),
          Actions.deserialize(tempo[2]),
          Actions.deserialize(tempo[3]),
          Actions.deserialize(tempo[4])
        ])

    dialog.destroy()

  def save(self, *args):
    dialog = Gtk.FileChooserDialog("Salvar", self,
        Gtk.FileChooserAction.SAVE,
        (Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
         Gtk.STOCK_SAVE, Gtk.ResponseType.OK))

    response = dialog.run()
    if response == Gtk.ResponseType.OK:
      tempos = []
      for row in self.store:
        tempos.append([r.serialize() for r in row[2:]])

      f = open(dialog.get_filename(), 'wb')
      f.write(pickle.dumps(tempos))
      f.close()

    dialog.destroy()


if __name__ == '__main__':
  dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

  bus = dbus.SystemBus()

  agent = Agent(bus, '/foo/agent')
  agent.set_exit_on_release(False)
  agentmanager = dbus.Interface(bus.get_object('org.bluez', '/org/bluez'),
      'org.bluez.AgentManager1')
  agentmanager.RegisterAgent('/foo/agent', 'KeyboardDisplay')

  profile = Profile(bus, '/foo/dance')
  profilemanager = dbus.Interface(bus.get_object('org.bluez', '/org/bluez'),
      'org.bluez.ProfileManager1')
  profilemanager.RegisterProfile('/foo/dance',
    RFCOMM_UUID, {
      'Name': 'Dancer',
      'Role': 'client',
      'RequireAuthenticaton': True,
      'RequireAuthorization': True,
    })

  GObject.type_register(Actions)
  GObject.type_register(WaitAction)
  GObject.type_register(TurnOnAction)
  GObject.type_register(TurnOffAction)
  GObject.type_register(FadeInAction)
  GObject.type_register(FadeOutAction)
  GObject.type_register(StrobeAction)

  window = MainWindow()
  window.show_all()

  btwindow = BluetoothWindow()
  btwindow.show_all()
  BluetoothDevice.start_discovery(btwindow)

  Gtk.main()
