#include "Particle.h"

#include "RgbStrip.h"
#include "NtcThermistor.h"
#include "clickButton.h"
#include "Settings.h"
#include "AudioPlayer.h"
#include "MP3Player.h"
#include "TonePlayer.h"
#include "VoicePulse.h"

//#define DEBUG_STARTUP_DELAY
#define SUPPORT_AUDIO_TONE
#define SUPPORT_MP3_PLAYBACK
#define SUPPORT_VOICE_DETECTION

#define WELCOME_VOICE "voice_welcome.mp3"
#define SUPER_STAR_MP3 "super_star.mp3"

#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3.h"

//Firmware version 1.1.00
PRODUCT_VERSION(1100);

// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(SEMI_AUTOMATIC);

// Run the application and system concurrently in separate threads
SYSTEM_THREAD(ENABLED);

//enable the reset reason feature
STARTUP(System.enableFeature(FEATURE_RESET_INFO));

//Default to the internall antenna
STARTUP(WiFi.selectAntenna(ANT_INTERNAL));

SerialLogHandler logHandler(LOG_LEVEL_ERROR);

//The particle logo on the front is a button - this is the controller for it
static constexpr int TOUCH_PIN = D10;
ClickButton particleButton(TOUCH_PIN, LOW, CLICKBTN_PULLUP);

//The controller for the LEDs and the mode
RgbStrip *rgbStrip = NULL;
static RgbStrip::MODES_T mode = RgbStrip::MODE_SNOWFLAKE;

//our settings controller
Settings settings = Settings();

//audio interface
AudioPlayer audioPlayer = AudioPlayer();

//mp3 player
MP3Player mp3Player = MP3Player(&audioPlayer);

//tone player
TonePlayer tonePlayer = TonePlayer(&audioPlayer);

//was sparkle detected?
static void sparkleDetectedCallback( void );
static bool sparkleMode = false;

//voice pulse. threshold of .72 is somewhat arbitariily chosen from testing - it might be too high / low
VoicePulse voicePulse = VoicePulse(&audioPlayer, sparkleDetectedCallback, 0.72f);

//list of songs to play and index of the current song
std::vector<String> songs;
uint32_t songIndex = 0;

void setup()
{
    // //wait for usb  to connect
    #ifdef DEBUG_STARTUP_DELAY
        waitFor(Serial.isConnected, 10000);

       delay(10000);
    #endif

    rgbStrip = new RgbStrip();

    //load our settings file
    settings.init();

    //get the led mode and set the mode variable
    String ledMode = settings.get("ledMode");
    if (ledMode.length() > 0) {
        mode = (RgbStrip::MODES_T)ledMode.toInt();
        rgbStrip->setMode(mode);
    }

    //configure the touch button
    pinMode(TOUCH_PIN, INPUT_PULLUP);

    // Setup button timers (all in milliseconds / ms)
    // (These are default if not set, but changeable for convenience)
    particleButton.debounceTime   = 20;   // Debounce timer in ms
    particleButton.multiclickTime = 250;  // Time limit for multi clicks
    particleButton.longClickTime  = 1000; // time until "held-down clicks" register

    // find all mp3 files in the assets system disk and create a list of them for later
    auto assets = System.assetsAvailable();
    for (auto& asset: assets)
    {
        if (asset.name().endsWith(".mp3"))
        {  
            //don't add WELCOME_VOICE or SUPER_STAR_MP3
            if( (asset.name() != WELCOME_VOICE) && (asset.name() != SUPER_STAR_MP3) ) {
                songs.push_back(asset.name());
                //Log.info("Found song: %s", asset.name().c_str());
            }
        }
    }

    //hardware watchdog
    Watchdog.init(WatchdogConfiguration().timeout(10s));
    Watchdog.start();

  #ifdef SUPPORT_AUDIO_TONE
      Log.info("Reset reason: %d", System.resetReason());

      const auto resetReason = System.resetReason();

      switch( resetReason )
      {
          case RESET_REASON_PIN_RESET:
          case RESET_REASON_USER:
          case RESET_REASON_POWER_DOWN:
              //play a two-tone beep boop when booting up only from a cold power on or USB reset
              //tonePlayer.play( TonePlayer::TONE_SEQUENCE_BOOT );

              //play the welcome mp3
              //don't play this in the updated version, but leave the code in here incase someone wants to modify
              //mp3Player.play(WELCOME_VOICE, 100);
          break;
      }
  #endif

  #ifdef SUPPORT_VOICE_DETECTION
      //start the voice pulse
      voicePulse.start();
  #endif

  //Connect to the particle platform!
  //This will run asynchronously
  Particle.connect();
}


bool mp3IsPlaying = false;

void loop()
{
    static bool localSparkleMode = false;

    // Update button state
    particleButton.Update();

    //kick the watchdog
    Watchdog.refresh(); 

    //If we are in 'sparkle' mode, run the basic sparkle animation and play the audio, detecting when it finishes and then resuming the previous animation mode
    if( sparkleMode && !localSparkleMode ) {
        //only start sparkle mode if we are not already playing an MP3
        if( !mp3IsPlaying ) {
            //entering sparkle mode 
            rgbStrip->setMode(RgbStrip::MODE_SPARKLE);

            //start the mp3
            mp3Player.play(SUPER_STAR_MP3, 100, [&](const bool playing){
                mp3IsPlaying = playing;
            });

            localSparkleMode = true;
        }
        else {
            Log.info("MP3 is already playing, not starting sparkle mode");
            sparkleMode = false;
        }
    }
    else if( sparkleMode && localSparkleMode ) {
        //has the mp3 finished?
        if( !mp3IsPlaying ) {
            //exit sparkle mode
            sparkleMode = false;
            localSparkleMode = false;

            //restore the previous led mode
            rgbStrip->setMode(mode);
        }
    }

    //ignore the buttons if we are in sparkle mode
    if( !localSparkleMode ) {
        //switch on particleButton.clicks
        switch( particleButton.clicks ) 
        {
            case 1:
                Log.info("SINGLE click");
                //inc mode
                mode = (RgbStrip::MODES_T)((mode + 1) % RgbStrip::MODE_MAX);

                // don't allow these to be selected by default as its just used for bootup or the secret..
                if( (mode == RgbStrip::MODE_OFF) || (mode == RgbStrip::MODE_SPARKLE) ) { 
                    mode = RgbStrip::MODE_SNOWFLAKE;
                }
                rgbStrip->setMode(mode);

                //store the updated setting
                settings.set("ledMode", String(mode));
                settings.store();

                #ifdef SUPPORT_AUDIO_TONE
                    //play a two-tone beep boop when switching the display mode
                    //this will fail if already playing a song
                    tonePlayer.play( TonePlayer::TONE_SEQUENCE_TWO_TONE );
                #endif
            break;

            case 2:
                Log.info("DOUBLE click");
            break;

            case 3:
                Log.info("TRIPLE click");
            break;

            case -1:
                Log.info("SINGLE LONG click");

                #ifdef SUPPORT_MP3_PLAYBACK
                    if( !mp3IsPlaying ) {
                        //play the next song in the list
                        //it allows max of 1 item to be queued and provides a callback
                        mp3Player.play(songs[songIndex], 100,  [&](const bool playing){
                            mp3IsPlaying = playing;
                        });
                        songIndex = (songIndex + 1) % songs.size();
                    }
                #endif
            break;
        }
    }
}


static void sparkleDetectedCallback( void ) {
    Log.info("Sparkle Detected!");
    
    //set the sparkle mode
    sparkleMode = true;
}
