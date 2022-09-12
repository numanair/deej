# How-To: Reassign MIX5R Pro MIDI Faders

This document describes the steps for changing what MIDI command each fader sends. Since most DAWs and other MIDI compatible software have a learning/mapping feature, this programming is not always required. Use the method(s) that works best for your use-case.

## Limitations

During this process make sure no other *serial* software (such as Deej) attempts to connect to the mixer. Serial supports only one connection at a time. The MIDI connection is separate, so that can safely remain active.

If you have a 1st edition mixer (shipped before May 20th, 2022), please refer to the documentation here: [Legacy Documentation](https://github.com/numanair/deej/blob/stm32-logic/Docs/reassign%20MIDI%20via%20serial.md)

## Features / Versions

### v1.1.0

- Add user defined min/max limits to fader outputs. Toggle modes with `m`
- Add help menu accesible from `h`

### v1.0.0

- Automatically saves MIDI CC and channel settings, even if the mixer is unplugged
- Check current settings with `c`
- Check firmware version with `v`
- Temporarily pause (and resume) Deej output with `d`

### Legacy

- Per-session MIDI settings
- Simultaneous Deej & MIDI support (also present in later releases)
- Mixers shipped before May 20th, 2022

## Fader Assignment Steps

![CoolTerm serial demo](https://github.com/numanair/deej/blob/stm32-logic-saving/Docs/Images/Animation_trimmed_ff.gif)

### 1. Setup Serial Tools

Fader assignment is done through serial instead of MIDI. This allows for reassigning faders while in-use.

Any serial terminal can be used, but for this tutorial we will use CoolTerm. The free download is available here: <https://www.freeware.the-meiers.org/> *NOTE:* The regular downloads are on the left. Don't mistakenly download an older or different version from the middle section.

![CoolTerm Download](https://github.com/numanair/deej/blob/stm32-logic-saving/Docs/Images/CoolTerm-dl.png)

### 2. Connect The Terminal

Open CoolTerm and click *Options*. Make sure the baud rate is set to 9600 (usually the default). Choose the correct port from the serial port options. The correct port will vary depending on a number of factors. If you are using Windows, you may check device manager to find which port is assigned. After choosing a port, click the connect button. You will know you have the right port when you click connect and see a stream of numbers formatted like so: 1023|0|0|460|120

If you are on Windows and do not see a COM port for the mixer, please follow these [driver installation instructions](https://github.com/numanair/deej/blob/stm32-logic/Docs/Windows%20Driver%20Install%20for%20MIDI%20Mixer.md).

To pause this output and better see the messages from the mixer, send `d`. If you wish to resume this output for use with Deej, simply send `d` again.

![CoolTerm ports](https://github.com/numanair/deej/blob/stm32-logic-saving/Docs/Images/coolterm_ports.png)

### 3. Create and Send the New Configuration

Here is an example of MIDI settings used to reassign the faders.  

```bat
<07,07,19,21,11:01,02,01,01,01>
```  

The above example assigns the two leftmost faders to MIDI CC 7 (volume) for channel 1 and 2. The remaining faders are assigned to CC 19, 21 and 11 (all channel 1). Please reference your favorite list of MIDI CCs.

The format for fader assignment is a list of CCs followed by a list of channels. They are assigned to the faders left-to-right. The two lists are separated by a colon and start and end with angle brackets.

To send this configuration to the mixer, use the "send string" feature of CoolTerm. I recommend saving your new configuration in a text file and pasting it into the terminal. This makes it easier to change it or send it again. To send the string, either copy it and paste it directly into the main window, or click *Send String* from the *Connection* menu.

If you want to make a small change, first send the character `c` to retrieve the current settings. The terminal will print out the settings, which you can copy, modify and send back to reprogram.

![CoolTerm connect and send](https://github.com/numanair/deej/blob/stm32-logic-saving/Docs/Images/coolterm_connect+send.png)

As soon as you send the string, the mixer will report back the new settings in the terminal. After a small delay the settings are saved to the MIX5R Pro.

If you are satisfied with the new settings you may close the serial terminal (CoolTerm). If it asks to save it is safe to click no.

### Output Limits Assignment

New in version 1.1.0 is the ability to set min/max limits for each fader's output. To switch to limits assignment mode, use `m`. If you are already in Limits Mode `m` will switch back to CC/channel assignment mode. Setting limits works like setting CC's. The format is <lower_limit:upper_limit>.
For example, the default full range of MIDI output is:  
```bat
<0,0,0,0,0:127,127,127,127,127>
```  
Each output can also be reversed by swapping the minimum and maximum values.
