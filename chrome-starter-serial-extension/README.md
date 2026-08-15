# DogDog Starter Serial Bridge

Chrome extension that reads the current starter from the webmelden result entry page and sends it to the DogDog controller over Web Serial.

## Install

1. Open `chrome://extensions`.
2. Enable `Developer mode`.
3. Click `Load unpacked`.
4. Select this `chrome-starter-serial-extension` directory.
5. Click the DogDog extension icon. A bridge tab opens.
6. Click `Connect Serial` and select the DogDog controller serial port at `115200` baud.

For testing with the saved local HTML file, open the extension details page and enable `Allow access to file URLs`.

## Protocol

The extension sends one CRLF-terminated line whenever the current starter changes:

```text
competitor|First name|Last name|Dog name
```

Example from the sample page:

```text
competitor|Sandra|RUPPERT|Zuma
```

The controller parses that line and forwards it to the timepanel as a competitor update.
