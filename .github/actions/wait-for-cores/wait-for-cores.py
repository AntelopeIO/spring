import dbus
import time

bus = dbus.SystemBus()
systemd = bus.get_object("org.freedesktop.systemd1", "/org/freedesktop/systemd1")
systemd_iface = dbus.Interface(systemd, dbus_interface="org.freedesktop.systemd1.Manager")

while len(systemd_iface.ListUnitsByPatterns([], ["systemd-coredump@*.service"])):
   print("Waiting for core dump collection to complete...")
   time.sleep(1)

