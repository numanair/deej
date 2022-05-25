# How-To: Reassign MIX5R MIDI Faders

This document describes the steps for changing what MIDI command each fader sends. Since most DAWs and other MIDI compatible software have a learning feature, this programming is not always required. So make sure you are not skipping an easier method before following these steps.

## Limitations

During this process make sure no other **serial** software attempts to connect to the mixer automatically. Serial only supports one connection at a time (it is seperate from the MIDI connection, so that can safely remain active).

If you have a 1st edition mixer (shipped before May 20th, 2022), unplugging or rebooting will cause the mixer to return to the default configuration. If possible, use mapping features within your audio software. This version shows up as "Maple" in your software.

## Fader Assignment Steps

![CoolTerm serial demo](https://github.com/numanair/deej/blob/stm32-logic/Docs/Images/Animation_trimmed_ff.gif)

### 1. Setup Serial Tools

Fader assignment is done through serial instead of MIDI. Any serial terminal can be used, but for this tutorial we will use CoolTerm. The free download is available here: <https://www.freeware.the-meiers.org/> *NOTE:* The regular downloads are on the left. Don't mistakenly download an older or different version from the middle section.

![CoolTerm Download](https://github.com/numanair/deej/blob/stm32-logic/Docs/Images/CoolTerm-dl.png)

### 2. Connect The Terminal

Open CoolTerm and click *Options*. Make sure the baud rate is set to 9600 (usually the default). Choose the correct port from the serial port options. The correct port will vary depending on a number of factors. After choosing a port, click the connect button. You will know you have the right port when you click connect and see a stream of numbers formatted like so: 1023|0|0|460|120

![CoolTerm ports](https://github.com/numanair/deej/blob/stm32-logic/Docs/Images/coolterm_ports.png)

### 3. Create and Send the New Configuration

Here is an example of MIDI settings used to reassign the faders.  

```bat
<07,07,19,21,11:01,02,01,01,01>
```  

The above example assigns the two leftmost faders to MIDI CC 7 (volume) for channel 1 and 2. The remaining faders are assigned to CC 19, 21 and 11 (all channel 1). Please reference your favorite list of MIDI CCs.

The format for fader assignment is a list of CCs followed by a list of channels. They are assigned to the faders left-to-right. The two lists are separated by a colon and start and end with angle brackets.

To send this configuration to the mixer, use the "send string" feature of CoolTerm. I recommend saving your new configuration in a text file and pasting it into the terminal. This makes it easier to change it or send it again. To send the string, either copy it and paste it into the main window, or click *Send String* from the *Connection* menu.

![CoolTerm connect and send](https://github.com/numanair/deej/blob/stm32-logic/Docs/Images/coolterm_connect+send.png)

As soon as you send the string, the mixer will report back the new settings in the terminal. The stream of numbers will resume after a moment, but MIDI control should be uninterrupted even before that.  

If you are satisfied with the new settings you can close the serial terminal (CoolTerm). If it asks to save it is safe to click no.
