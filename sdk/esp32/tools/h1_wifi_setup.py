"""Configure the ESP32-S3 through its CH340 / USB2 port, without storing a password in files."""
import argparse
import getpass
import json
import time
from h1_wifi_pc import serial_module

def exchange(port, command, timeout=6):
    port.reset_input_buffer()
    raw = ('H1CFG/1 ' + command + '\n').encode('utf-8')
    if len(raw) >= 1024:
        raise ValueError('configuration command is too long')
    if port.write(raw) != len(raw):
        raise OSError('short serial write')
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline()
        marker = line.find(b'H1CFG/1 {')
        if marker < 0:
            continue
        try:
            reply = json.loads(line[marker+8:])
        except (ValueError, UnicodeError):
            continue
        if reply.get('protocol') == 'H1CFG/1':
            if not reply.get('ok'):
                raise RuntimeError(reply.get('message', 'ESP32 rejected command'))
            return reply
    raise TimeoutError('No H1CFG/1 reply. Check USB2, firmware and COM port.')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', required=True, help='ESP32 USB2 / CH340 port; enumerate it before use')
    ap.add_argument('action', choices=['status','set','clear'], nargs='?', default='status')
    args = ap.parse_args()
    serial = serial_module();port = serial.Serial(port=None, baudrate=115200, timeout=0.3, write_timeout=2)
    port.dtr = False;port.rts = False;port.port = args.port
    try:
        port.open();time.sleep(0.2)
        info = exchange(port, 'INFO')
        if info.get('target') != 'esp32s3' or info.get('firmware') not in ('h1-wifi/0.1.0', 'h1-wifi/0.2.0', 'h1-wifi/0.3.0', 'h1-wifi/0.3.1', 'h1-wifi/0.3.2', 'h1-wifi/0.3.3', 'h1-wifi/0.4.0', 'h1-wifi/0.4.1', 'h1-wifi/0.4.2', 'h1-wifi/0.4.3', 'h1-wifi/0.4.4'):
            raise RuntimeError('Unexpected firmware identity; configuration was not changed.')
        if args.action == 'set':
            ssid = input('2.4 GHz Wi-Fi name (SSID): ')
            password = getpass.getpass('Wi-Fi password (empty for open network): ')
            if not 1 <= len(ssid.encode('utf-8')) <= 32 or (password and not 8 <= len(password.encode('utf-8')) <= 63) or '\0' in ssid+password:
                raise ValueError('SSID: 1..32 bytes. Password: empty or 8..63 bytes. No NUL.')
            print(exchange(port, 'SET ' + json.dumps(dict(ssid=ssid,password=password), ensure_ascii=False))['message'])
        elif args.action == 'clear':
            print(exchange(port, 'CLEAR')['message'])
        else:
            print(json.dumps(info, ensure_ascii=False, indent=2))
    finally:
        port.close()

if __name__ == '__main__':
    main()
