# Introduction

FreeDV GUI (or just FreeDV) is a GUI program for Linux, Windows, and
macOS for running FreeDV on a desktop PC or laptop.

This is a live document.  Notes on new FreeDV features are being added as they are developed. 

# Getting Started

This section contains instructions to help you get started.

## System Requirements

FreeDV is officially supported on the following operating systems and versions:

* Windows: Windows 10 and later
* macOS: macOS Big Sur (11.0) and later (ARM and Intel)
* Linux: Ubuntu 22.04 LTS and later, Fedora 42 and later

While it may be possible to execute FreeDV on additional platforms and distributions 
(especially ones similar to ones currently supported), this is not guaranteed. Any 
bugfixes required for an unsupported platform may be deferred or rejected, especially 
if there is impact to supported platforms. Requests for official support of new platforms 
should go through our existing [Feature Request process](https://github.com/freedv/freedv-gui/issues/new?template=feature_request.yml).

## Setup Wizard

Upon starting FreeDV for the first time, the Setup Wizard dialog will appear. This
is a streamlined setup process for FreeDV optimized for hardware commonly used
by amateur radio operators and is divided into four sections:

1. Receive audio,
2. Transmit audio,
2. CAT/PTT control, and
3. Reporting.

Note that you can always return to this dialog by going to *Settings->Setup Wizard*.

### Sound Card Configuration

To configure your sound card(s) using the Setup Wizard, simply select the sound device(s)
associated with your radio and the microphone and speaker devices you wish to use
to hear decoded audio as well as to transmit audio. If you're setting up a receive-only
station, you can check "Receive only" when prompted.

### CAT/PTT control

Setup Wizard supports three methods of radio control:

1. No radio control (e.g. using a VOX audio device such as SignaLink),
2. Hamlib or OmniRig CAT control, and
3. Serial port PTT control.

Simply select the option that matches your radio setup and the required fields will
appear. For Hamlib, these are typically the type of radio you're using as well as the
serial port it's connected to (or TCP/IP hostname:port). Serial port PTT control requires 
the serial port your radio is using as well as whether your radio's PTT is triggered
via the RTS or DTR pin (and the required polarity for either).

### Reporting

While not required, it is recommended to enable reporting so that others
can see who is currently receiving them. This also allows the FreeDV
application to control the radio's frequency and mode. Both sides of a contact 
must have this enabled in order for contacts to be reported. To configure 
reporting, simply enable the feature here and enter your callsign and current 
grid square/locator.

For more information about the reporting feature, see the "FreeDV Reporting" section below.

## Advanced Setup

### Sound Card Configuration

#### Receive Only (One Sound Card)

For this setup, you just need the basic sound hardware in your computer, 
for example a microphone/speaker on your computer.

1. Open the *Setings - Edit Settings* Dialog
1. At the bottom select *RX Audio* Tab
1. In *Input To Computer From Radio* select your default sound input device (usually at the top)
1. In the *Output From Computer To Speaker/Headphones* window select your default sound output device (usually at the top)
1. At the bottom select *TX Audio* Tab
1. Check "Receive only"
1. Press OK to close the dialog

When you press Start FreeDV will start decoding any incoming signals
on the microphone input, playing the decoded audio out of your
speaker.  If no valid FreeDV signals are received, no audio will be
played.

If you connect the microphone input on your computer to your radio
receiver, you can decode off air signals.  If you have a rig
interface, try configuring that as the *From Radio To Computer*
device, with your computer's sound card as the *From Computer To
Speaker/Headphone* device.

If you don't have anyone to transmit FreeDV signals to you, try the
test wave files in the next section.

#### Transmit/Receive (Two Sound Cards)

For TX/RX operation you need to configure two sound cards, by setting
up Tools - Audio Config *Transmit* and *Receive* Tabs.

When receiving, FreeDV off-air signals **from** your radio are decoded
by your computer and sent **to** your speaker/headphones, where you
can listen to them.

When transmitting, FreeDV takes your voice **from** the microphone,
and encodes it to a FreeDV signal in you computer which is sent **to**
your radio for transmission over the air.

Tab | Sound Device | Notes
--------------- | ---------------------------------------------- | ----------------------------------------------
Receive Tab | Input To Computer From Radio | The off air FreeDV signal **from** your radio rig interface to your computer
Receive Tab | Output From Computer To Speaker/Headphones | The decoded audio from your computer to your Speaker/Headphones
Transmit Tab | Input From Microphone To Computer | Your voice from the microphone to your computer
Transmit Tab | Output From Computer To Radio | The FreeDV signal from your computer sent **to** your rig interface for TX

#### Changing Audio Devices

If you change audio devices (e.g. add or remove sound cards, USB hardware), it's a good idea to check the Tools/Audio Config dialog before pressing **Start**, to verify the audio devices are as expected. This is particularly important if any audio devices e.g. Headsets, USB Sound Cards, or Virtual Cables have been disconnected since the last time FreeDV was used.

Hitting **Refresh** in the lower left hand corner of the Tools/Audio Config will normally update the audio devices list. Keeping a screen shot of a known working configuration will be useful for new users. Unexpected audio configuration changes may also occur following a Windows updates.

Another solution is to re-start FreeDV and check Tools/Audio Config again after changing any audio hardware.

### CAT/PTT Configuration

The Tools->CAT and PTT Config dialog supports three different ways to control PTT on
your radio:

+ VOX: sends a tone to the left channel of the Transmit/To Radio sound card
+ Hamlib: support for many different radios via the Hamlib library and a serial port (or via TCP/IP for some devices, e.g. SDRs or FLrig/rigctld).
+ Serial Port: direct access to the serial port pins

You may also optionally configure an additional serial port for PTT input.
This can be useful for interfacing devices like foot switches to 
FreeDV. If configured, FreeDV will switch into transmit mode (including
sending the needed Hamlib or serial commands to initiate PTT) when it
detects the configured signal.

Once you have configured PTT, try the **Test** button.

Serial PTT support is complex.  We get many reports that FreeDV
PTT doesn't work on a particular radio, but may work fine with other
programs such as Fldigi.  This is often a mismatch between the serial
parameters Hamlib is using with FreeDV and your radio. For example you
may have changed the default serial rate on your radio. Carefully
check the serial parameters on your radio match those used by FreeDV
in the PTT Dialog.

Also see [Common Problems](#common-problems) section of this manual.

#### Hamlib

Hamlib comes with a default serial rate for each radio.  If your radio
has a different serial rate change the Serial Rate drop down box to
match your radio.

When **Test** is pressed, the "Serial Params" field is populated and
displayed.  This will help track down any mismatches between Hamlib
and your radio.

If you are really stuck, download Hamlib and test your radio's PTT
using the command line ```rigctl``` program.

#### Icom Radio Configuration 

If using an Icom radio, Hamlib will use the radio's default CI-V address
when connecting. If this has been changed, you can specify the correct
address in the "Radio Address" field (valid values are 00 through FF
in hexadecimal). 

Note that "00" is the "wildcard" CI-V address. Your radio must have the 
"CI-V Transceive" option enabled in order for it to respond to commands
to that address. Otherwise, FreeDV must be configured to use the same
CI-V address as configured in the radio. For best results, ensure that
there are no other Icom/CI-V capable devices in the chain if 
"00"/"CI-V Transceive" is used.

#### Changing COM Port On Windows

If you change the COM port of a USB-Serial device in Device Manager,
please unplug and plug back in the USB device.  Windows/FreeDV won't
recognize the device on the new COM Port until it has been
unplugged/plugged.

## Test Wave Files

In the installation are audio files containing off-air FreeDV modem signals. 
There is one file per FreeDV mode and are in the following locations depending 
on platform:

| Platform | Typical Location                                             |
|----------|--------------------------------------------------------------|
| Windows  | C:\\Program Files\\FreeDV [version]\\share\\freedv-gui\\wav  |
| Linux    | /usr/share/freedv-gui/wav or /usr/local/share/freedv-gui/wav |
| macOS    | See https://github.com/freedv/freedv-gui/tree/master/wav    |

To play these files, first select a FreeDV mode and press Start.  Then 
choose a file using "Tools - Start/Stop Play File From Radio".  You should 
then hear decoded FreeDV speech.

These files will give you a feel for what FreeDV signals sound like,
and for the basic operation of the FreeDV software.

## Sound Card Levels

Sound card levels are generally adjusted in the computer's Control
Panel or Settings, or in some cases via controls on your rig interface
hardware or menus on your radio. In-app adjustments can also be done
using the **TX Attenuation** control on the right-hand side of the main
window. The `<<` and `>>` buttons adjust by 1.0 dB and the `<` and `>`
buttons adjust by 0.2 dB; the mouse scroll wheel can also be used to 
generate 0.2dB steps whilst hovering anywhere over the TX Attenuation
control area. Values below 0 dB attenuate the transmit signal.

When FreeDV is running, you can observe the sound card signals in the
main window tabs (From Radio, From Mic, To Mod, To Speaker).

1. On receive, FreeDV is not very sensitive to the **From Radio**
level, adjust so that the Level meter is around 75% scale and not clipping.  FreeDV uses phase
shift keying (PSK) so is not sensitive to amplitude.

1. The transmit level from your computer to your radio is important.
On transmit, adjust your level so that the ALC is **just** being
nudged.  More **is not better** with the FreeDV transmit signal.
Overdriving your transmitter will lead to a distorted transmit signal, and
a poor SNR at the receiver.  This is a very common problem.

   Right-clicking on the **TX Attenuation** control top row opens a context menu
   with the following options for the currently active amateur band:

   * **Enable auto-save of TX atten for [band]** — saves the current level for
     that band and enables automatic saving whenever the band is left or the
     program is closed. The menu item changes to **Disable auto-save of TX atten
     for [band]** once enabled, which removes the saved value and stops auto-saving.
   * **Restore TX atten level for [band]** — reverts the attenuation to the last
     auto-saved value for that band, useful for recovering a known good level
     after experimenting during a session.
    
   The **Control** panel (right-hand side) contains a **Tune** button which outputs a continuous 1500 Hz carrier for antenna matching, netting etc.
   While Tune is active the TX Attenuation controls adjust the tune signal level rather than the normal transmit level.

   Right-clicking the Tune button opens a context menu with the following options:
   * **Set tune output to minimum (-30 dB)** — immediately sets the tune attenuation to its lowest output level without transmitting, useful for safe low-power antenna tuner testing.
   * Per-band **Enable/Disable auto-save** and **Restore** options, similar to those described above for TX attenuation (shown only when a band is known).
   
   FreeDV will automatically detect band changes (via Hamlib or OmniRig CAT, or manual
   frequency entry) and load any saved attenuation value for the new band.
   When a band is left the current values for that band are saved immediately.
   
 1. FreeDV 700D and 700E can drive your transmitter at an average power of 40% of its peak power rating.  For example 40W RMS for a 100W PEP radio. Make sure your transmitter can handle continuous power output at these levels, and reduce the power if necessary.

1. Adjust the microphone audio using your operating system's volume settings so the peaks are not clipping, 
and the average is about 75% of max scale on the Level meter. If additional adjustments are needed, the "Mic Level"
slider on the right hand side of the main window will allow you to adjust audio levels +/- 20 dB. The "From Mic" tab 
will show audio after Mic Level adjustments, but before AGC (if enabled). If additional adjustments are performed,
the peaks in this plot should not exceed approximately +/- 0.5.  

1. AGC is enabled by default and is recommended that it remain on. If desired, it can be disabled by going to Tools->Filters.
The "To Mod" plot shows the audio that will ultimately be transmitted out over the air, after AGC (if enabled) processing.

## Audio Processing

FreeDV likes a clean path through your radio.  Turn all audio
processing **OFF** on transmit and receive:

+ On receive, DSP noise reduction should be off.

+ On transmit, speech compression should be off.

+ Keep the receive audio path as "flat" as possible, no special filters.

+ FreeDV will not work any better if you band pass filter the off air
received signals.  It has its own, very tight filters in the
demodulator.

## USB or LSB?

FreeDV has standardized on upper sideband for RADEV2, regardless of the location in the band. As an aid to this, FreeDV will show the current mode on the bottom of the window upon pressing the Start button if Hamlib is enabled and your radio supports retrieving frequency and mode information over CAT. If your radio is using an unexpected mode, it will display that mode on the bottom of the window next to the Clear button in red letters. When a session is not active, Hamlib isn't enabled, or if your radio doesn't support retrieving frequency and mode over CAT, it will remain grayed out with "unk" displaying instead of the mode (for "unknown").

Note that if in Analog mode, FreeDV will use the appropriate sideband for the band.  To ensure that the same portion of spectrum is used by both digital and analog modes FreeDV will adjust the radio frequency by +3kHz on the legacy LSB bands. i.e. 40, 80 and 160 meters. (For example, 7177 kHz becomes 7180 kHz when the Analog button is pushed.)

## Transceiver Filters

For most FreeDV use, your radio's receive and transmit filters should be set to the widest possible (typically around 3 kHz). 
This allows easy switching between analog mode as well as the various FreeDV modes. There has not been any testing done to
definitively confirm whether narrower bandwidths help with reception of FreeDV. Additionally, FreeDV already performs its own 
transmit filtering, so using additional narrow filtering on the radio will likely have little benefit (again, untested).

# Voice Keyer

The Voice Keyer Button on the front page puts FreeDV and your radio into 
transmit, reads a wave file of your voice to call CQ, and then switches to 
receive to see if anyone is replying.  If you press the space bar or click
the XMIT button, the voice keyer stops.  If a signal with a valid sync is 
received for a few seconds the voice keyer also stops.

The Audio tab inside Settings-Edit Settings can be used to select the wave file, set 
the RX delay, and number of times the TX/RX cycle repeats.

Additional options are also available if you right-click on the Voice Keyer button:

* "Use another voice keyer file": Allows you to trigger the voice keyer using a different file from the one configured in Settings->Edit Settings.
* "Record new voice keyer file": Triggers transmit and records your microphone audio to a file that you select. Pushing the Voice Keyer button will stop recording.
* "Monitor transmitted audio": Allows monitoring of audio while transmitting the voice keyer file. If enabled, a checkmark will appear next to this option.

# Monitoring TX Audio

FreeDV has the ability to monitor transmit audio. This can be useful for adjusting 
microphone filters when your radio is plugged into a dummy load. To enable this,
right-click on the XMIT button and choose "Monitor transmitted audio". A checkmark
will appear next to this menu option when enabled.

# Quick Record

To quickly record incoming signals from the radio, a 'Record' button is provided
in the main window. Clicking this button opens a dialog that lets you choose what
to record:

* "Off Air": records the raw audio coming in from the radio (while transmitting,
  the modulated audio being sent to the radio instead). Files are named
  "FDV_FromRadio" followed by the current date and time, plus an optional suffix.
* "Decoded": records the decoded audio coming out of the demodulator. Files are
  named "FDV_FromDecoder" in the same way. Decoded recordings can optionally be
  saved as MP3.
* "Both": records both of the above at the same time to two separate files, both
  as 16-bit 48 kHz WAV. Note that the decoded recording only starts once the
  modem syncs, and lags the off-air recording by the modem's processing delay,
  so the two files can be aligned in an audio editor. While transmitting in
  half-duplex mode the decoded recording is silent (or contains the monitored
  microphone audio if "Monitor transmitted audio" is enabled).

Clicking 'Record' again will stop recording (all active files if "Both" was
chosen).

All recordings are 16-bit, 48 kHz, mono.

The Audio tab inside Settings-Edit Settings allows control of where these recordings are
saved. By default, this is inside the current user's Documents folder.

# Multiple Configurations

By default, FreeDV uses the following locations to store configuration:

* Linux: $XDG_CONFIG_HOME/freedv/freedv.conf (see below)
* macOS: ~/Library/Preferences/FreeDV\ Preferences
* Windows: Registry (HKEY\_CURRENT\_USER\\SOFTWARE\\freedv)

On Linux, XDG_CONFIG_HOME defaults to ~/.config if not explicitly overridden. Note that 
if you are not running the AppImage, the behavior is different if your distro does not 
include wxWidgets 3.3 or newer. In those cases, the default configuration file location
is ~/.freedv, which is the same as in prior versions of FreeDV.

If you'd like to store the configuration in another location (or store multiple configurations),
FreeDV accepts the -f (or --config) command line arguments to provide an alternate location. An
absolute path is recommended here; however, if only a relative path is provided, it will be relative
to the following locations:

* Linux: ~/
* macOS: ~/Library/Preferences/
* Windows: C:\\Users\\[username]\\AppData\\Roaming

You can also save and restore different configuration files by using the Tools->Export Configuration
and Use Configuration menu items. These menu options allow for switching of configurations without
restarting FreeDV. Note that restoring a configuration file does not modify the default configuration.
However, the last restored configuration file will be loaded the next time FreeDV starts, unless you
choose Tools->Load Default Configuration or restore a different configuration file.

## Executing FreeDV With a Different Configuration (Windows)

On Windows, you can create shortcuts to FreeDV with different file names for the "-f" command line
option as described above. To create a shortcut, right-click on the Desktop or in File Explorer and 
choose New->Shortcut. Click on Browse and navigate to one of the following paths:

* C:\\Program Files\\FreeDV [version]\\bin\\freedv.exe
* C:\\Program Files (x86)\\FreeDV [version]\\bin\\freedv.exe (if the 32 bit version is installed on a 64 bit machine)

Click Next and give the shortcut a unique description (e.g. "FreeDV IC-7300"). Then push Finish to create the shortcut.

Once the shortcut has been created, right-click it and choose Properties. Find the Shortcut tab in the resulting dialog
box and add "-f" followed by the desired filename to the end of the text in the Target field. Do not add any other
quote marks.

For example, to use a file called IC7300.conf stored in the Hamradio directory on the C drive the Target field should 
appear as follows:

"C:\\Program Files\\FreeDV [version]\\bin\\freedv.exe" -f C:\\Hamradio\\IC7300.conf

# FreeDV Reporting

FreeDV has the ability to send FreeDV signal reports to various online spotting services
by enabling the option in Settings-Edit Settings (in the Reporting tab) and specifying your callsign 
and Maidenhead grid square. When enabled, this causes FreeDV to disable the free form **Txt Msg** 
field and only transmit the **Callsign** field. As this uses a different encoding format 
from the free-form text field, both sides of the contact must have this enabled for the 
contact to be reported.

FreeDV validates the received information before submitting a position report. This 
is to ensure that FreeDV does not report invalid callsigns to the service (e.g. ones that don't exist 
or that correspond to real non-FreeDV users). However, if the reporting function is disabled,
all received text will display in the main window even if it has errors.

The following services are currently supported and can be individually enabled or disabled
along with the reporting feature as a whole:

* [PSK Reporter](https://pskreporter.info/) (using the "FREEDV" mode)
* [FreeDV Reporter](https://qso.freedv.org/) -- also accessible via the Tools->FreeDV Reporter menu option.

The frequency that FreeDV reports is set by changing the "Report Frequency" drop down box in the main window. This 
is either in kilohertz (kHz) or megahertz (MHz) (configurable by going to Settings->Edit Settings->Rig Control and checking
or unchecking "Frequency entry in kHz") and will turn red if the entered value is invalid. If Hamlib support is 
also enabled, this frequency will automatically remain in sync with the current VFO on the radio (i.e. if the 
frequency is changed in the application, the radio will also change its frequency). Double-clicking on users in 
the Tools->FreeDV Reporter window will also cause this frequency to change to match the other user.

By default, pushing Start sets the radio to the frequency FreeDV last used, and pushing Stop returns the radio to the
frequency and mode it had before Start. To leave the radio's frequency alone instead (FreeDV shows the radio's current
frequency on Start, and the radio stays where it is on Stop), uncheck "Save and restore the frequency on start and stop"
in Settings->Edit Settings->Rig Control. This applies to both Hamlib and OmniRig.

*Note: in some setups (such as when using ALE), it is not preferred to have the reporting frequency automatically be 
in sync with the radio. For example, in the case of ALE, the radio's frequency changes multiple times per second while
waiting for a contact, which is faster than FreeDV can pull the latest from the radio (every five seconds). This can 
be disabled by enabling "Manual Frequency Reporting" in Settings->Edit Settings.*

The FreeDV Reporter window includes a **Message:** field that allows you to broadcast a
short status message (such as your location, antenna, or current activity) to other FreeDV
Reporter users. Type a message and click **Send** to broadcast it, or **Clear** to remove
the current message.

*Note: messages are only broadcast when reporting is enabled in Settings->Edit Settings->Reporting
and the **Reporting** toggle button in the main window is on.*

Messages are not saved automatically. To save a message for later reuse, right-click the
**Send** button to open a context menu with the following options:

* **Send and save message** — broadcasts the message and adds it to the saved list
* **Only save message** — adds the message to the saved list without sending it

Saved messages are kept in a drop-down list of up to 15 entries, sorted alphabetically.
Click the arrow on the **Message:** field to view and select from saved messages.
Right-clicking on any entry in the list opens a context menu with options to:

* **Add** a new message to the list
* **Edit** the selected message
* **Delete** the selected message from the list

FreeDV will also show the callsigns of previously received signals. To view those, click on the arrow
next to the last received callsign at the bottom of the window. These are in descending order by time
of receipt (i.e. the most recently received callsign will appear at the top of the list).

# QSO Logging

FreeDV supports integration with external logging utilities that support the WSJT-X logging protocol (for example,
[KLog](https://github.com/ea4k/klog). This can be enabled by going to Settings->Edit Settings->Reporting and enabling 
the "Enable QSO Logging" option. By default, it sends packets to "localhost" (127.0.0.1) port 2237, but this
can be changed if the logging computer is elsewhere on the network.

Once FreeDV decodes a callsign, it will appear in the drop-down list on the bottom of the main window. To log
this contact (or any other contact received by the application), simply choose that callsign from the list and
click the "Log QSO" button on the left side of the window. You will then see a dialog box that will let you 
enter any additional information about the contact. Upon clicking the OK button, this information will be 
transmitted to your logging tool.

You can also log a contact if they're using FreeDV Reporter. Simply left-click on a row in the FreeDV Reporter
window and then click on the Log QSO button. This will autofill the callsign, grid square/locator and frequency
from the FreeDV Reporter servers in the dialog box.

The last selected logging source (either the FreeDV Reporter window or the drop-down callsign list at the bottom
of the main window) is used for auto-filling of logging data. This is to avoid confusion (for instance, if a row 
in the main window is selected but it's intended to log from FreeDV Reporter instead).

# Time-Out Timer (TOT)

FreeDV contains a built-in time-out timer (TOT) that prevents the radio from transmitting for longer than the configured
period. By default, this is enabled and set to 180 seconds (3 minutes). Fifteen seconds prior to the timeout firing,
a window will appear along with a beep in your configured speakers or headset to allow you to either reset the timer
or finish up your transmission. 

If you desire a different timeout or want to disable the TOT, you can do so by going to Settings->Edit Settings->Rig Control
and changing the TOT related options in that tab.

# FreeDV Modes

The following table is a guide to the different modes, using
analog SSB and Skype as anchors for a rough guide to audio quality:

Mode | Min SNR | Fading | Latency | Speech Bandwidth | Speech Quality
--- | :---: | :---: | :---: | :---: | :---:
SSB | 0 | 8/10 | low | 2600 | 5/10
RADEV1 | -2 | 8/10 | medium | 8000 | 7/10
Skype | - |- | medium | 8000 | 8/10

The Min SNR is roughly the SNR where you cannot converse without
repeating yourself.  The numbers above are on channels without fading
(AWGN channels like VHF radio).  For fading channels the minimum SNR
is a few dB higher. The Fading column shows how robust the mode is to
HF Fading channels, higher is more robust.

## FreeDV RADEV1

RADE is a new mode with state-of-the-art performance, it is a contraction of Radio AutoencoDEr so-named because the modulation encoding uses a Machine-Learning method where the modulation and demodulation are achieved by training on a large number of speech samples over a modelled radio channel with typical propagation disturbances in phase and amplitude as found in HF radio. The speech is synthesised using the FARGAN neural vocoder (Frame-wise Auto-Regressive GAN) where GAN is a Generative Adversarial Network).

Unlike the previous FreeDV modes which all use QPSK modulation, the RADEV1 modulation produces an analog phase-amplitude output without defined constellation points. It has been chosen as it offers a combination of high speech quality together with reduced RF bandwidth and good resistance to fading and multipath HF radio channels.

When using RADEV1, the squelch settings in the main window are ignored. Instead, FreeDV only passes decoded audio if it's able to synchronize with the incoming signal.

# Tools Menu

## Tools - Filter

This section describes features on Tools-Filter.  

Control | Description
 -------------------------- | ------------------------------------------------------------------------ |
Noise Suppression | Enable noise suppression, de-reverberation, AGC of mic signal using the Speex pre-processor

Auto EQ (Automatic Equalization) adjusts the input speech spectrum to best fit the speech codec. It can remove annoying bass artifacts and make the codec speech easier to understand.

Bass/Mid/Treble (for both microphone and speaker) can also be adjusted here, either with the arrow keys or your
mouse. A graph displaying the frequency response of the signal based on your current settings will display in
this window as well.

* [Blog Post on Auto EQ Part 1](http://www.rowetel.com/?p=6778)
* [Blog Post on Auto EQ Part 2](http://www.rowetel.com/?p=6860)

## Settings - Edit Settings

### Modem Options

Control | Description
 ------------------------------ | ----------------------------------------------------------------------------- |
Clipping | Increases the average power. Ensure your transmitter can handle high RMS powers before using!
TX Band Pass Filter | Reduces TX spectrum bandwidth

# Helping Improve FreeDV

If you have an interesting test case, for example:

1. FreeDV working poorly with a particular person or microphone.
1. Poor over the air performance on a fast fading channel.
1. Problems with sync on strong signals.
1. A comparison with SSB.

Please send the developers an off air recording of the signal.  FreeDV can record files from your radio using Tools-Record File from Radio.  A recording of 30 to 60 seconds is most useful.

With a recording we can reproduce your exact problem.  If we can reproduce it we can fix it. Recordings are much more useful than anecdotes or subjective reports like "FreeDV doesn't work", "SSB is better", or "On 23 December it didn't work well on grid location XYZ".  With subjective reports problems are impossible to reproduce, cannot be fixed, and you are unlikely to get the attention of the developers.

# Multiple Panes in GUI window

It is possible to have multiple panes opened within the GUI window for example, to observe both the Waterfall and Spectrum Tabs. New panes may be added above, below, left or right of existing panes.

A new visible pane is created by hovering the cursor over the required Tab, click and hold the left mouse button and drag the Tab to the required position and releasing the mouse button. If currently two panes are stacked vertically a third pane may be added either beside either pane or to the left/right of both panes.  If the Tab is required adjacent to both panes then it must be dragged to the left/right of the junction of the existing Tabs.

As the Tab is dragged into position a faint blue/gray image will show the position to be occupied by the pane. Panes may be relocated back to the menu bar by a similar process.

Tabs can be resized as required by hovering the cursor over the border and clicking and holding the left mouse button and dragging to required size.

By default, the tab layout is not saved. However, by enabling experimental features (see "Advanced/Developer Features"), experimental support for
saving and restoring the tab layout will be enabled. This has not been well tested and may have hidden bugs or other issues.

![Multiple Panes](contrib/multiple_panes.png)

# Advanced/Developer Features

## Stats Window

Located on the lower left hand side of the main screen.

Term | Notes
--- | --- |
Bits | Number of bits demodulated
Errs | Number of bit errors detected
Resyncs | Number of times the demodulator has resynced
ClkOff | Estimated sample clock offset in parts per million
FreqOff | Estimated frequency offset in Hz
Sync | Sync metric

The sample clock offset is the estimated difference between the
modulator (TX) and demodulator (RX) sample clocks.  For example if the
transmit station sound card is sampling at 44000 Hz and the receive
station sound card 44001 Hz, the sample clock offset would be
((44000-44001)/44000)*1E6 = 22.7 ppm.

## Sound Card Debug

These features were added to help diagnose sound card issues during development.

### Settings - Edit Settings dialog:

Debug FIFO and PortAudio counters: used for debugging audio
problems.  

The PortAudio counters (PortAudio1 and PortAudio2) should not
increment when running in TX or RX, as this indicates samples are
being lost by the sound driver which will lead to sync problems.

The Fifo counter outempty1 counter should not increment during
TX, as this indicates FreeDV is not supplying samples fast enough to
the PortAudio drivers.  The results will be resyncs at the receiver.

Check these counters by pressing Start, then Reset them and observe
the counters for 30 seconds.

If the PortAudio counters are incrementing on receive try:

  1. Adjusting framesPerBuffer; try 0, 128, 256, 512, 1024.

  1. Shut down other applications that might be using audio, such as
  Skype or your web browser.

  1. A different sound card rate such as 44.1kHz instead of 48kHz.

  If the outempty1 counter is incrementing on transmit try increasing
  the FifoSize.

  The txThreadPriority checkbox reduces the priority of the main txRX
  thread in FreeDV which may help the sound driver thread process
  samples.

  The txRXDumpTiming check box dumps timing information to a console.
  Each number is how many ms the txRXThread took to run.

  The txRXDumpTiming check box dumps the number of samples free in the
  TX FIFO sending samples to the TX.  If this hits zero, your TX audio
  will break up and the RX will lose sync.  TX audio break up will
  also occur if you see "outfifo1" being incremented on the "Fifo"
  line during TX.  Try increasing the FifoSize.

## Full Duplex Testing with loopback

Settings - Edit Settings - Half Duplex check box

FreeDV GUI can operate in full duplex mode which is useful for
development or listening to your own FreeDV signal as only one PC is
required.  Normal operation is half duplex.

TX and RX signals can be looped back via an analog connection between
the sound cards.

On Linux, using the ALSA loopback module:
```
  $ sudo modprobe snd-aloop
  $ ./freedv

  In Tools - Audio Config - Receive Tab  - From Radio select -> Loopback: Loopback PCM (hw:1,0)
                          - Transmit Tab - To Radio select   -> Loopback: Loopback PCM (hw:1,1)
```

## Experimental Features

Occasionally there are some features that may be unstable or otherwise not ready to officially be released.
To experiment with these features, you can go to Settings->Edit Settings->Debugging and enable "Enable Experimental
Features". Note that if you have problems after enabling this setting, it is recommended to disable it again to
verify that an experimental feature is not causing trouble. (Additionally, if you're able to confirm that
an experimental feature is causing problems, please file a bug report!)

# Tips

1. The space bar can be used to toggle PTT.
1. For modes other than RADEV1, you can left click on the main window to adjust tuning. The vertical red line on the frequency scale will show the current center frequency and FreeDV will automatically track any drift once it syncs.

# Common Problems

## FreeDV Sets Radio To Wrong Mode

By default, FreeDV attempts to set the radio's mode to DIGU/USB-D. Some radios do not support data modes and only have USB. For these, you can go to Settings->Edit Settings->Rig Control and check the
"Use USB instead of DIGU" option. This will cause FreeDV to use the standard USB mode for rig control instead.

Note that for best results, your radio should have all processing disabled if you're using the standard USB mode. This
disabling of processing typically takes place when using data mode.

## Overdriving Transmit Level

This is a very common problem for first time FreeDV users.  Adjust your transmit levels so the ALC is just being nudged. More power is not better with FreeDV.  An overdriven signal will have poor SNR at the receiver.  

## I can't set up FreeDV, especially the Sound Cards

This can be challenging the first time around:

1. Try a receive only (one audio card) set up first.

1. Ask someone who already runs FreeDV for help.

1. If you don't know anyone local, ask for help on the digital voice
mailing list.  Be specific about the hardware you have and the exact
nature of your problem.

## Hamlib does not work with my Icom radio

The most common issue with Icom radios is that the CI-V address configured
in FreeDV does not match the address configured in the radio. Ensure that
the CI-V address in both FreeDV and on the radio are the same. If "00" is
used on the FreeDV side, ensure that the "CI-V Transceive" option is enabled
on the radio or else the radio will not respond to requests directed to that
address.

On newer radios (e.g. 7300, 7610), you may also need to set "CI-V USB Echo Back" 
to ON as this may be set to OFF by default.

## Hamlib seems not reliable on my Linux machine

Some Linux distributions come with pre-installed software that may try to use
serial ports that your radio creates. You may have problems initialising your radio
or CAT control via hamlib may seem unreliable or time out.

*Brltty* is software that looks on serial ports for a Braille device which will also
upset CAT communication with your radio. Typically this is seen as a timeout error windw
from hamlib.

Stop BRLTTY as follows:

```
sudo systemctl stop brltty-udev.service
sudo systemctl disable brltty.service
sudo systemctl mask brltty-udev.service
```

BRLTTY is the most common cause of communication problems but it's also been reported
that in some cases ModemManager can give trouble.

*ModemManager* is on some Ubuntu based distributions and it will try to reset a modem
by sending AT commands which will upset your radio.

Stop ModemManager as follows:

```
sudo systemctl stop ModemManager
sudo systemctl disable ModemManager
sudo systemctl mask ModemManager
```

## I'm using OmniRig and have problems with rig control

If you're using OmniRig and have problems getting rig control to work, please attempt
to update the INI files for OmniRig. These can be found by going [here](https://dxatlas.com/Download.asp)
and clicking on the "INI files for OmniRig" link. There may also be a newer OmniRig version
available at that same link which could work better if you are running an older version.

## I need help with my radio or rig interface

There are many radios, many computers, and many sound cards.  It is
impossible to test them all. Many radios have intricate menus with
custom settings.  It is unreasonable to expect the authors of FreeDV to
have special knowledge of your exact hardware.

However someone may have worked through the same problem as you.  Ask
on the digital voice mailing list.

## Can't hear anything on receive

Many FreeDV modes will not play any audio if there is no valid signal.
You may also have squelch set too high.  In some modes the **Analog**
button will let you hear the received signal from the SSB radio.

Try the Test Wave Files above to get a feel for what a FreeDV signal
looks and sounds like.

## Hearing myself whenever I transmit

Verify audio device settings in FreeDV. If your audio settings are correct,
there are a few other possibilities:

1. If on Windows, make sure all audio effects are disabled on all devices used with FreeDV.
2. Turn off TX monitoring (right-click on XMIT button and make sure "Monitor transmitted audio" is unchecked).

## The signal is strong but FreeDV won't get sync and decode

Do you have the correct sideband? See USB or LSB section.

Is it a FreeDV signal?  SSTV uses similar frequencies. To understand what FreeDV sounds like, see the Test Wave Files section.

## PTT doesn't work.  It works with Fldigi and other Hamlib applications.

Many people struggle with initial CAT control and PTT setup:

1. Read the "CAT/PTT Configuration" section above.

1. Try the "Test PTT" button inside Tools->CAT and PTT Config.

1. Check your rig serial settings.  Did you change them from defaults
for another program?

1. Linux version: do you have permissions for the serial port?  Are you a member
of the ```dialout``` group?

1. Ask someone who already uses FreeDV to help.

1. Contact the digital voice mailing list.  Be specific about your
hardware, what you have tried, and the exact nature of the problem.

## I'm on Windows and serial port PTT doesn't work with my USB to serial adapter.

Please verify that you are running the correct drivers for the USB to serial adapter
that you're using. Information and download links for the drivers used by the most
common devices can be found [here](https://www.miklor.com/COM/UV_Drivers.php). 

While it is preferred to use devices that use authorized/original versions of the
various USB to serial chipsets, it is possible to use some cloned devices with 
older drivers. When doing this, you may also need to force Windows to use an older 
version of a driver instead of automatically updating the driver on reboot. See
[here](https://wethegeek.com/how-to-disable-automatic-driver-updates-in-windows-10/)
for instructions on doing so in Windows 10. For Windows 8:

1. Search for "Change device" in the Windows 8 Start menu.
1. Click on where it says "Change device installation settings".
1. Select the "No, let me choose what to do" option.
1. Check the "automatically get the device app" option, then click Save changes to save the settings you just chose.

## I installed a new version and FreeDV stopped working

You may need to clean out the previous configuration.  Try Tools - Restore Defaults.  Set up your sound cards again with Tools - Audio Config.

## FreeDV crashes when I press Start

Have you removed/changed USB audio devices? If you remove/change USB audio devices without pressing Tools - Audio Config, FreeDV may crash.  See Changing Audio Devices above.

## FreeDV can't be opened on macOS because the developer cannot be verified

From January 2020 Apple is enforcing notarization for all macOS applications. While official releases of FreeDV are signed,
other types of builds may not be. This may result in a dialog box similar to the below:

![Notarization Error](contrib/osx_notarization1.png)

(or on later releases of macOS) a message indicating that the FreeDV binary is "corrupt". To override the notarization requirement
and execute FreeDV, run the following from Terminal:

```
xattr -d com.apple.quarantine FreeDV.app
```
or
```
xattr -d -r com.apple.quarantine FreeDV.app
```

## I'm on a Mac and my preferred audio device isn't appearing

macOS has a longstanding issue that prevents certain audio devices from appearing. More info [here](http://www.w1hkj.com/files/fldigi/AppleAudioCodec.html).

## FreeDV doesn't preserve window positions on Linux

Many Linux distributions are switching away from X11 and towards a new GUI system known as Wayland.
Wayland explicitly does not support window positioning by applications, instead leaving it to the user's
compositor to control window positioning. You can force use of X11 via the Xwayland compatibility layer
by adding `GDK_BACKEND=x11` to your environment to regain the ability to preserve window positioning
if desired.

# Converting this document to PDF

For the Linux inclined:
```
$ pandoc USER_MANUAL.md -o USER_MANUAL.pdf "-fmarkdown-implicit_figures -o" \
--from=markdown -V geometry:margin=.4in --toc --highlight-style=espresso
```

# Glossary

Term | Notes
------- | ---------------------------------------------------------------------------------------------
AWGN | Additive White Gaussian Noise - a channel with just noise and no fading (like VHF)
FEC | Forward Error Correction - extra bits to we send to protect the speech codec bits
LDPC | Low Density Parity Check Codes - a family of powerful FEC codes

# Release Notes

## V3.0.0 TBD 2026

1. Enhancements:
    * RADEV2: Standardize mode as USB. (PR #1397, #1454) - thanks @barjac!
    * Combine all configuration into Settings->Edit Settings. (PR #1418, #1511)
    * Rework Easy Setup window into new Setup Wizard. (PR #1418, #1432, #1513)
    * Add ability to record both the raw and decoded RX audio. (PR #1501)
    * Linux/PulseAudio: Show friendlier device names in configuration windows. (PR #1508)
    * Add ability to import configuration from WSJT-X/WS/JTDX/JS8Call. (PR #1513, #1539)
    * Add dark mode support for Windows. (PR #1518, #1529, #1560)
    * Add infrastructure for translating FreeDV into other languages. (PR #1526)
    * Hamlib: only allow selecting a PTT serial device when using RTS or DTR for PTT. (PR #1544)
    * Improved ease of use when adjusting predefined frequencies. (PR #1570, #1581) - thanks @barjac!
    * Improved Level metering and AGC for easier input level adjustment. (PR #1532, #1599, #1604) - thanks @barjac!
    * Hamlib/OmniRig: add option to disable saving and restoring the radio's frequency on start and stop. (PR #1600)
2. Bugfixes:
    * Fix silence being mixed into the first half second of each transmission, and holes in the signal at its start. (PR #1562)
3. Build system:
    * Update wxWidgets to 3.3.4. (PR #1560, #1556)
    * CI: only build with PGO, run the tests and report coverage on merges; other pushes get unsigned packages to try out. (PR #1563, #1585)
4. Other:
    * Remove legacy FreeDV modes (700D/700E/1600). (PR #1407, #1411, #1415)
    * Add automated tests of the user interface: Setup Wizard, FreeDV Reporter, Settings, main, Log QSO, Start Recording and Filter windows, plots, audio device errors, serial port PTT and rig control. (PR #1544, #1546, #1548, #1550, #1551, #1553, #1554, #1556, #1588, #1595)
    * Run the automated user interface tests, and report test coverage, as part of CI. (PR #1556)
    * Run the automated user interface tests on macOS. (PR #1563)
    * Run the automated user interface tests on Windows. (PR #1566)
    * Remove unused plot, main window and FreeDV Reporter code. (PR #1556)
    * Settings: play back audio input tests through the speaker/headphones instead of the radio. (PR #1548)
    * Add tests of whole transmissions through the TX/RX threads without sound cards, and diagnostics for rade_loss test failures. (PR #1562)

*Note: Legacy FreeDV modes (700D/700E/1600) have been removed due to low use. FreeDV 2.4.x is still available if one wishes to operate these modes.*

## V2.4.1 TBD 2026

1. Bugfixes:
    * Add logic to prevent FIFO sizes that are too small to allow TX thread to function. (PR #1474)
    * Fix RX/TX level-meter gauge contamination. (PR #1475) - thanks @barjac!
    * Fix Hamlib-related build failure with Hamlib 5.0~git. (PR #1477)
    * macOS: Fix dyld error on startup when enabling sanitizers in build. (PR #1478)
    * Fix potential equalizer bug that could introduce corrupted audio. (PR #1480)
    * Fix uninitialized value read during waterfall plot render. (PR #1481)
    * Match std::atomic memory ordering to what each atomic actually synchronises. (PR #1482)
    * Remove use of mutexes in audio path during callsign decode. (PR #1486, #1493)
    * Tighten audio thread timings on macOS and Windows. (PR #1494)
    * Harden audio FIFO handling and WASAPI stop timing. (PR #1496)
    * Fix crash when testing Easy Setup with an invalid audio configuration. (PR #1500)
    * Various data race cleanups. (PR #1502)
    * Avoid page cache miss on startup when loading RADE. (PR #1506)
    * Stop playback of RX file before TX start. (PR #1507)
    * Fix horizontal lines in waterfall on HiDPI/fractionally scaled Linux displays. (PR #1514)
    * Reduce GUI thread paint and FreeDV Reporter overhead. (PR #1515, #1530)
    * PulseAudio: give each device its own connection. (PR #1523)
    * Fix possible deadlock on playback completion. (PR #1533)
    * Windows: add logic to detect audio device stalls. (PR #1537)
    * Keep RX muted after TX until the radio confirms PTT off. (PR #1536)
    * Fix FreeDV hanging when starting the Voice Keyer with a cloud-backed file. (PR #1534)
    * Fix data races on settings read by the audio threads. (PR #1535)
    * Prevent a closed network connection from terminating FreeDV with SIGPIPE. (PR #1540)
    * Fix equalizer bass/treble filters being designed for the wrong sample rate. (PR #1542)
    * Fix frequency list entries occasionally being saved 1 Hz low. (PR #1542)
    * Fix text settings containing e.g. "$HOME" being altered when loaded. (PR #1542)
    * Hamlib/OmniRig: fix frequency/mode requested prior to connecting not being applied. (PR #1542)
    * Reject stereo files in "Play File - From Radio" instead of playing them at half speed. (PR #1542)
    * Fix possible crashes when stopping while using a serial port for PTT input. (PR #1542)
    * Hamlib: fix limit on time spent waiting for the radio not always being applied. (PR #1542)
    * FreeDV Reporter: fix incorrect distances/headings when one's own grid square is incomplete. (PR #1542)
    * FreeDV Reporter: fix stations sorting in the wrong order when their frequencies are more than about 2.1 GHz apart. (PR #1558)
    * Fix crash when closing FreeDV right after stopping with a radio connected. (PR #1543)
    * FreeDV Reporter: fix idle filter menu check marks after cancelling a custom idle time. (PR #1547)
    * FreeDV Reporter: disable Request QSY once a request has been sent. (PR #1547)
    * Voice keyer: fix button not showing "Stop Voice Keyer" while the voice keyer is running. (PR #1549)
    * Voice keyer: fix newly recorded file being forgotten the next time FreeDV starts. (PR #1549)
    * Fix Load Default Configuration reloading the configuration in use instead of the default one. (PR #1549)
    * Linux: fix Load Default Configuration loading an empty configuration rather than the default one. (PR #1557)
    * Logging: fix logged contacts sometimes being timed a second early. (PR #1552)
    * Fix FreeDV being unable to exit after pressing Start with a sound device that couldn't be opened. (PR #1559)
    * Fix possible crash when an audio device fails to open before FreeDV has asked to be told of its errors. (PR #1559)
    * Linux: fix audio pausing for up to two more seconds after FreeDV's audio processing has been held up for more than two. (PR #1559)
    * Settings: fix a typed-in frequency losing its last three digits when switching from kHz to MHz and pressing Apply. (PR #1559)
    * Linux: fix crash on pressing Start with no sound server (PipeWire/PulseAudio) running. (PR #1555)
    * Linux: report losing the sound server while running, and fix Stop getting stuck afterwards. (PR #1555)
    * Fix each use of Adjust Monitor Volume leaving a hidden popup behind. (PR #1555)
    * Filter: fix equaliser changes being ignored if the window was opened before pressing Start. (PR #1552)
    * Fix each use of Log QSO leaving a hidden window behind. (PR #1552)
    * Recording: restrict a pasted recording suffix to the same characters as a typed one. (PR #1552)
    * FreeDV Reporter: fix a direction of 0 being shown for stations without a usable grid square after changing the band filter. (PR #1564)
    * macOS: fix the last block of audio being sent to the radio over and over after transmitting, rather than silence. (PR #1564)
    * Fix a saved main window size too small to be of use being kept. (PR #1564)
    * Rig control: fix FreeDV briefly going back to the previous frequency after changing it (and so e.g. choosing the wrong sideband for analog mode when switched to straight after changing band). (PR #1564)
    * macOS/Windows: fix possible crash or garbled log output when some audio device and thread errors are logged. (PR #1564)
    * Windows: fix the main window being too small to show the waterfall and spectrum the first time FreeDV is started on a high DPI display. (PR #1565)
    * Windows: fix FreeDV Reporter not removing what's left of a flag from the status message after part of it is deleted. (PR #1565)
    * Fix a possible crash when FreeDV is asked to exit again while it's exiting with the FreeDV Reporter window open. (PR #1565)
    * Fix Test PTT sometimes staying greyed out after a radio or serial port that couldn't be opened. (PR #1565)
    * Fix silence being mixed into the first half second of each transmission. (PR #1567, #1568)
    * Fix the voice keyer's RX pause and wait for a reply, the slow SNR display and the statistics reset after losing sync running twice as fast as intended. (PR #1571)
    * Fix the spectrum plot's sync tuning line being drawn using an uninitialized value until the plot's first update. (PR #1572)
    * FreeDV Reporter: fix stations that connected or transmitted within the same second sorting in a random order. (PR #1573)
    * Fix a data race on whether FreeDV is running, between the window and the thread that starts and stops it. (PR #1575)
    * Fix data races between the window, audio and processing threads on squelch, resync count, sound card count, PortAudio counters, debug switches, recording state and EQ filter re-apply flags. (PR #1578)
    * Fix data races on the transmit mode selection when changed while FreeDV is running, and on the queue holding received text characters. (PR #1578)
    * Linux: Fix Options frequency list scrollbar on first open. (PR #1579)
    * Fix data races between the window and the threads that start/stop FreeDV and process audio on the frequency offset display, the list of active reporters and the audio device latency check at the end of TX. (PR #1583)
    * macOS: fix a possible crash when an audio device that failed to start, or was already stopped, is stopped again. (PR #1583)
    * Windows: fix audio devices closing their timing handles while the audio processing thread could still be waiting on them, and undoing the 1 ms timer resolution request twice. (PR #1583)
    * Prevent duplicate contacts from being restored. (PR #1589)
    * Disable Log QSO button if logging is disabled. (PR #1589)
    * Keep a typed FreeDV Reporter status message when the saved list closes. (PR #1590)
    * Stop the radio recording budget at zero instead of wrapping it. (PR #1592)
    * Windows: Fix issue preventing audio devices that don't support raw mode from starting. (PR #1594)
    * PlotScalar: draw the trace when the plot is wider than its sample count. (PR #1597) - thanks @barjac!
2. Build system:
    * Windows versions are now built with llvm-mingw 20260908 (PR #1489)
    * Ccache support extended to third party dependencies. (PR #1498, #1505))
    * Fix bundled libsndfile build on lib64 systems and strncpy build error. (PR #1510)
    * Only use system libraries that can actually be linked. (PR #1521)
    * Optimizations to reduce full rebuild time. (PR #1538)
    * Build bundled wxWidgets and LibreSSL optimized and without debugging information, and only use ccache in CI for the builds that compile them. (PR #1561)
    * Add unit tests for non-GUI code. (PR #1542)
    * Add tests of whole transmissions through the TX/RX threads without sound cards. (PR #1567)
    * Resolve build failure on OpenSUSE Tumbleweed. (PR #1577)
3. Documentation:
    * Update documentation to reflect new repo URL. (PR #1569)
4. Other:
    * Various GUI performance improvements. (PR #1481, #1521, #1524, #1527, #1531)
    * Windows audio thread timing improvements. (PR #1488)
    * Log when the sound server puts a PulseAudio stream on another device. (PR #1591)

## V2.4.0 August 2026

1. Bugfixes:
    * Hamlib: Allow two timeouts during connection process to allow Icom marine radios to behave better. (PR #1369, #1388)
    * Rename sanitizers.h->freedv_sanitizers.h due to name conflicts. (PR #1372)
    * Fix intermittent non-response to short PTT clicks to stop TX. (PR #1375) - thanks @barjac!
    * Revert to libsamplerate library to fix remaining audio quality issues. (PR #1379)
    * Right-justify SNR column in FreeDV Reporter to improve appearance. (PR #1387) - thanks @barjac!
    * Hamlib: Set frequency again on mode changes. (PR #1395)
    * Consolidate EOO length calculation in freedv-backend to improve callsign decode reliability. (PR #1402)
    * Hamlib: Switch off memory channel before setting frequency/mode. (PR #1403) - thanks @barjac!
    * Hamlib/OmniRig: Round received frequency to nearest 100 Hz. (PR #1373)
    * Tighten remaining audio settings to improve audio drops. (PR #1412)
    * Fix corruption of string config arrays containing commas. (PR #1417) - thanks @barjac!
    * Suppress button flicker in Linux light themes. (PR #1419, PR #1421) - thanks @barjac!
    * Only preserve previously selected tab on TX if it's in the same group as 'From Mic'. (PR #1428)
    * Scalar plot label alignment fix for Y axis of Frm Decoder/Mic/Radio, SNR and Spectrum plots. (PR #1429) - thanks @barjac!
    * Fix window position restore under KWin and labwc (main window + FreeDV Reporter window). (PR #1431, #1433, #1449, #1450) - thanks @barjac!
    * Harden experimental tab layout persistence. (PR #1434) - thanks @barjac!
    * Fix right-click context menus dismissing before they can be read (GTK) (PR #1437) - thanks @barjac!
    * Unconditionally add new station to Heard Station list if first heard. (PR #1444)
    * Fix heard-stations callsign combo stuck-highlight, right-click behaviour, and a stale-index crash (PR #1448) - thanks @barjac!
    * Fix crash and long hang on main window close with an unresponsive rig (PR #1452) - thanks @barjac!
    * Fix Radio Frequency coloring on dark/light mode transition. (PR #1453)
    * Fix FreeDV Reporter column-order corruption and Last TX column width. (PR #1458) - thanks @barjac!
    * Fix waterfall/spectrum filling solid colour during half-duplex TX. (PR #1462) - thanks @barjac!
    * Windows/macOS: Zero audio on startup. (PR #1463)
    * Fix issue causing TX thread to use more CPU time than expected with no audio available. (PR #1465)
    * AppImage: stop forcing GDK_BACKEND=x11. (PR #1593)
2. Enhancements:
    * Add UDP broadcast of received callsigns. (PR #1367)
    * Add Time-Out Timer (TOT) capability to FreeDV. (PR #1366, #1398, #1405) - thanks @barjac!
    * Load last-used config file on restarts. (PR #1365, #1371)
    * Add "Set tune output to minimum" to Tune button context menu for safety. (PR #1378) - thanks @barjac!
    * Move Tune button into Control widget for improved usability. (PR #1377) - thanks @barjac!
    * Restore heard station list on launch. (PR #1358) - thanks @barjac!
    * Add momentary PTT option to FreeDV. (PR #1393)
    * Allow 8k sample rate for 'radio' devices. (PR #1416)
    * FreeDV Reporter: Connect to server via TLS by default. (PR #1422, #1427)
    * Block special characters in recording file name suffixes and default the voice keyer file selector to show .wav and .mp3. (PR #1424) - thanks @barjac!
    * Fix Voice Keyer/PTT context menu positioning under native Wayland. (PR #1438) - thanks @barjac!
    * Automatically start decoding on FreeDV startup. (PR #1436)
    * Remove "Clip" indicator from main window to reduce confusion. (PR #1461)
    * Use log scale for Level meter and take mic level before AGC. (PR #1464)
3. Build system:
    * Clear CMake deprecation warnings in FreeDV. (PR #1383, #1386)
    * Upgrade Hamlib to 4.7.2. (PR #1413)
    * Update wxWidgets to 3.3.3. (PR #1439)
    * Require C++20 to build FreeDV. (PR #1440)
    * Enable LTO/PGO for macOS .app build. (PR #1456)
    * Enable PGO for Windows builds. (PR #1457)
    * Add support for CCache. (PR #1328)
4. Other:
    * FlexRadio/KA9Q integrations moved to freedv-integrations repo. (PR #1368)
    * Improve tab save/load behavior on systems with wxWidgets 3.3+. (PR #1435)

## Earlier than V2.3.0

See [this](https://github.com/freedv/freedv-gui/blob/master/CHANGELOG_OLD.md) for more information about changes in versions prior to 2.3.0.

# References

* [FreeDV Web site](http://freedv.org)
* [FreeDV Technology Overview](https://github.com/drowe67/codec2/blob/master/README_freedv.md)
* [Digitalvoice mailing list](https://groups.google.com/forum/#!forum/digitalvoice)
 
