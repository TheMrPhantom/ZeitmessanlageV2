# Horn stop remote

This ESP32 firmware turns the existing remote into a dedicated stop control for
the horn project.

- The existing active-low button remains on GPIO 33 with its internal pull-up.
- Pressing the button sends the horn protocol's `stop` command (`0x03`) three
  times. The horn deduplicates these copies; the redundancy reduces the chance
  that radio interference loses the stop command.
- The command uses the same raw 802.11 frame format and horn MAC address as the
  dogdog-controller horn timer.
- The remote returns to light sleep after the button is released.
- A normal power-up does not send a command unless the button is held down.

The `Horn Wi-Fi channel` setting must match the horn project's
`Buzzer SoftAP and promiscuous channel` setting. Both default to channel 1.
