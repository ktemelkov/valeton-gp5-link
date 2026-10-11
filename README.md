# Valeton GP5 In Guitar Switch

A project for switching the presets on Valeton GP5 multi-effects processor directly from your guitar.
Modifications made to the guitar includes:
- removing tone potentiometer and replacing it with the volume potentiomenter
- installing a rotary encoder with a push button in the place of the volume potentiometer
- rewiring the pickup selector switch and disconnecting the neck pickup
- installing ESP32 C3 board for sending SysEx messages to the GP5 over BLE 

## Images

![Project Image 1](img/image1.png)

![Project Image](img/image.png)

## How it works

The adapter selection switch is wired to the ESP32-C3 board and is used to select preset switching logic.
The presets in the Valeton GP5 are organized into blocks of 30 positions. The middle 10 presets of each block are reserved for rythm tones and the lower and upper 10 presets can be used for clean and lead tones. The encoder push button initiates the preset switching itself.
When the button is pushed, the SysEx message for preset selection is sent to the GP5 with the preset number calculated using the following logic: 
- Bottom position of the switch: toggles between lower and middle 10 preset blocks 
- Middle position: switches on/off the delay module
- Top posisiton of the switch: toggles between upper and middle 10 preset blocks
For example if your rythm tone is on position 11 and the switch is at the bottom position the push button toggles between preset 11 and preset 1, and if the swich is at the top position the push button toggles between presets 11 and preset 21. So you can have 3 different tones per song and 10 positions for different song settings (30 presets block)

## Hardware

- Seeed Studio XIAO ESP32C3 board
- 500 mAh LiPo battery (lasts at least 5 hours of operation)
- Rotary encoder with push button

## Future

The rotary encoder can be used for preset selection, instead of using the pickup selection switch and disconnecting the bridge pickup.

## Serial Terminal PWA

The [sample web app](docs/index.html) is a USB serial terminal with an installable
[manifest](docs/manifest.json) and an offline [service worker](docs/sw.js).
It sends UTF-8 text and displays serial output; it does not implement GP5 commands,
BLE control, or firmware flashing. The connected firmware must support any commands
you send. The default baud rate is 115200 (8-N-1, no flow control).

### GitHub Pages

1. Push the `docs` folder, including its PNG assets, to GitHub.
2. In **Settings > Pages**, choose **Deploy from a branch**, select your branch,
	and choose **/docs** as the source folder.
3. Open `https://<owner>.github.io/<repository>/` in desktop Chrome or Edge.
4. Click **Connect device**, then select the USB serial port in the browser prompt.
	Close other serial monitors first so they do not hold the port open.
5. Install using **Install app** when offered, or the browser's install menu.

Web Serial requires HTTPS (or localhost), a supported browser, and explicit user
permission. Installing the PWA does not bypass those restrictions. Most mobile
browsers, Firefox, and Safari do not support Web Serial. Port permission is only
requested when you click Connect; the app never automatically reconnects.

### Local Preview

From the repository root, run `python -m http.server 8000 --directory docs`, then
open `http://localhost:8000/`. Opening the HTML directly with `file://` is not a
supported way to test the PWA. After the first successful online load, the app
shell is available offline; the USB device must still be connected for serial I/O.
The log is kept in memory (limited to 100,000 characters); use **Save log** to
download it before closing the app.

When updating cached assets, increment `CACHE_NAME` in the service worker. Close
all app tabs/windows and reopen to activate a newly installed worker.

## License

See LICENSE file for details.
