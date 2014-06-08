#!/usr/bin/python

  

if __name__ == '__main__':
  dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

  print("conectando no system bus")
  bus = dbus.SystemBus()
  
  print("criando agente")
  agent = Agent(bus, '/foo/agent')
  agent.set_exit_on_release(False)
  
  print("criando profile")
  profile = Profile(bus, '/foo/dance')

  print("registrando agente")
  agentmanager = dbus.Interface(bus.get_object('org.bluez', '/org/bluez'),
      'org.bluez.AgentManager1')
  agentmanager.RegisterAgent('/foo/agent', 'KeyboardDisplay')

  print("registrando profile")
  profilemanager = dbus.Interface(bus.get_object('org.bluez', '/org/bluez'),
      'org.bluez.ProfileManager1')
  profilemanager.RegisterProfile('/foo/dance',
    RFCOMM_UUID,  {
      'Name': 'Dancer',
      'Role': 'client',
      'RequireAuthenticaton': True,
      'RequireAuthorization': True,
    })

  if len(sys.argv) > 1:
    bd = BluetoothDevice(sys.argv[1])
  else:
    BluetoothDevice.start_discovery()

  loop = GObject.MainLoop()
  print("iniciando main loop da glib")
  loop.run()
