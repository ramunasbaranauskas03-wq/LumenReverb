#pragma once
#include <vector>
#include <juce_core/juce_core.h>

// =====================================================================================
// Lumen MixCheck — playback system models
//   every model = stereo handling + speaker / box EQ + driver non-linearity (with bass
//   excursion) + device DSP limiter + room / space + environment noise (+ special chains:
//   broadcast processing, streaming normalisation, custom IR)
// =====================================================================================
namespace lumen::mixcheck
{
enum class Cat { Off, Big, Car, Mobile, Home, Broadcast, Studio, Streaming, Custom };
enum Noise { NoNoise = 0, Crowd, Road, Street, Hiss, Crackle, Living };
enum Special { None = 0, FM, AM, Stream, Custom };
enum Icon { IcMonitors, IcClub, IcFestival, IcCar, IcCarSub, IcPhone, IcPhoneStereo, IcReels, IcEarbuds, IcPods, IcHeadphones,
            IcLaptop, IcBtSpeaker, IcTv, IcRadio, IcFm, IcAm, IcCeiling, IcWall, IcCube, IcNearfield, IcHifi, IcStream, IcIr };

struct Eq { int type; float f, g, q; };      // type 0 bell, 1 low shelf, 2 high shelf

struct Device
{
    const char* name; const char* shortName; Cat cat; int icon;
    // stereo
    float monoBelow = 0;          // Hz, lows summed to mono (0 = off)
    float width = 1;              // 0 mono .. 1 full
    float gainRdB = 0, delayRms = 0;   // listener position (car seat): right speaker level / delay
    // response
    float hp = 0; int hpOrder = 0; float lp = 0; int lpOrder = 0;
    std::vector<Eq> eq;
    // driver
    float driveDb = 0;            // into the soft limit of the driver (0 = clean)
    float excursion = 0;          // extra distortion from bass excursion (0..3)
    float exFreq = 150;           // bass region that drives the cone
    // device DSP limiter
    float limThr = 0, limRatio = 0, limAtt = 5, limRel = 150, limSc = 0;   // limSc: side-chain low pass (bass limiter)
    // space
    float room = 0, rt = 0.4f, size = 0.5f, predelay = 5, damp = 6000;
    // environment
    int noise = NoNoise; float noiseDb = -40;
    // special
    int special = None; float target = -14; bool boostAllowed = true;
    bool speaker = true;          // gets the headphone (speaker-virtualisation) treatment
    const char* listenFor = "";
};

inline const std::vector<Device>& devices()
{
    static const std::vector<Device> d = [] {
        std::vector<Device> v;
        auto add = [&] (Device x) { v.push_back (x); return &v.back(); };
        Device x;

        x = {}; x.name = "Headphones Only"; x.shortName = "HEADPHONES"; x.cat = Cat::Off; x.icon = IcHeadphones; x.speaker = false;
        x.listenFor = "No device simulation: only your headphone correction is on.  Your mix exactly as it is."; add (x);

        x = {}; x.name = "Studio Monitors (Virtual)"; x.shortName = "STUDIO"; x.cat = Cat::Studio; x.icon = IcMonitors;
        x.hp = 38; x.hpOrder = 2; x.eq = { { 0, 120, 1.0f, 1.0f } };
        x.room = 0.09f; x.rt = 0.25f; x.size = 0.35f; x.predelay = 2; x.damp = 9000;
        x.listenFor = "Your everyday mixing view on headphones: two flat nearfields in a treated room, in front of you - "
                      "panning, depth and the centre image like on real speakers."; add (x);

        x = {}; x.name = "Club PA"; x.shortName = "CLUB"; x.cat = Cat::Big; x.icon = IcClub;
        x.monoBelow = 120; x.hp = 30; x.hpOrder = 4; x.lp = 16000; x.lpOrder = 2;
        x.eq = { { 1, 75, 5.5f, 0.7f }, { 0, 52, 3.0f, 1.4f }, { 0, 260, -2.5f, 0.8f }, { 0, 3200, 1.5f, 1.0f }, { 2, 10000, -3.5f, 0.7f } };
        x.driveDb = 3; x.excursion = 0.4f; x.exFreq = 100; x.limThr = -6; x.limRatio = 6; x.limAtt = 2; x.limRel = 120;
        x.room = 0.24f; x.rt = 1.3f; x.size = 1.0f; x.predelay = 18; x.damp = 4200; x.noise = Crowd; x.noiseDb = -30;
        x.listenFor = "Does the kick still punch through the sub?  Is the low end mono and steady?  Harsh 2-4 kHz at club level?"; add (x);

        x = {}; x.name = "Festival Main Stage"; x.shortName = "FESTIVAL"; x.cat = Cat::Big; x.icon = IcFestival;
        x.monoBelow = 150; x.width = 0.85f; x.hp = 28; x.hpOrder = 4; x.lp = 14000; x.lpOrder = 2;
        x.eq = { { 1, 65, 7.0f, 0.7f }, { 0, 400, -2.0f, 0.7f }, { 0, 2500, 2.0f, 0.9f }, { 2, 7000, -5.0f, 0.6f } };
        x.driveDb = 4; x.excursion = 0.5f; x.exFreq = 90; x.limThr = -5; x.limRatio = 8; x.limAtt = 1; x.limRel = 100;
        x.room = 0.16f; x.rt = 2.6f; x.size = 1.0f; x.predelay = 85; x.damp = 2500; x.noise = Crowd; x.noiseDb = -26;
        x.listenFor = "Open air: the drop must hit with pure kick + sub.  Thin leads disappear, midrange decides energy."; add (x);

        x = {}; x.name = "Car (Driver Seat)"; x.shortName = "CAR"; x.cat = Cat::Car; x.icon = IcCar;
        x.width = 0.75f; x.gainRdB = -2.5f; x.delayRms = 1.6f; x.hp = 38; x.hpOrder = 2;
        x.eq = { { 1, 70, 6.0f, 0.7f }, { 0, 140, 2.0f, 1.0f }, { 0, 420, -3.0f, 0.9f }, { 0, 2200, -2.0f, 1.0f }, { 2, 6500, -3.0f, 0.7f } };
        x.driveDb = 1; x.excursion = 0.3f; x.exFreq = 120;
        x.room = 0.14f; x.rt = 0.13f; x.size = 0.15f; x.predelay = 1; x.damp = 5000; x.noise = Road; x.noiseDb = -30;
        x.listenFor = "Cabin gain pumps the bass: is it boomy?  Can you still hear the vocal / lead over road noise?"; add (x);

        x = {}; x.name = "Car + Subwoofer"; x.shortName = "CAR + SUB"; x.cat = Cat::Car; x.icon = IcCarSub;
        x.width = 0.75f; x.gainRdB = -2.5f; x.delayRms = 1.6f; x.hp = 24; x.hpOrder = 4;
        x.eq = { { 1, 55, 10.0f, 0.7f }, { 0, 42, 4.0f, 1.6f }, { 0, 500, -2.5f, 0.8f }, { 2, 8000, 2.5f, 0.7f } };
        x.driveDb = 2; x.excursion = 1.4f; x.exFreq = 80; x.limThr = -8; x.limRatio = 4; x.limAtt = 5; x.limRel = 200; x.limSc = 120;
        x.room = 0.12f; x.rt = 0.13f; x.size = 0.15f; x.predelay = 1; x.damp = 5000; x.noise = Road; x.noiseDb = -33;
        x.listenFor = "Boom car: does the sub distort or rattle?  Too much sub energy eats the whole mix."; add (x);

        x = {}; x.name = "Phone Speaker"; x.shortName = "PHONE"; x.cat = Cat::Mobile; x.icon = IcPhone;
        x.width = 0; x.hp = 480; x.hpOrder = 4; x.lp = 11000; x.lpOrder = 4;
        x.eq = { { 0, 900, 3.0f, 1.4f }, { 0, 2600, 5.0f, 2.0f }, { 0, 6200, -4.0f, 1.5f } };
        x.driveDb = 8; x.excursion = 2.5f; x.exFreq = 700; x.limThr = -14; x.limRatio = 8; x.limAtt = 1; x.limRel = 80;
        x.room = 0.05f; x.rt = 0.25f; x.size = 0.2f; x.damp = 7000;
        x.listenFor = "No sub at all: does the kick still click and the bass still read through its harmonics?"; add (x);

        x = {}; x.name = "Phone (Stereo, Flagship)"; x.shortName = "PHONE ST"; x.cat = Cat::Mobile; x.icon = IcPhoneStereo;
        x.width = 0.45f; x.hp = 320; x.hpOrder = 4; x.lp = 15000; x.lpOrder = 4;
        x.eq = { { 0, 1200, 2.0f, 1.0f }, { 0, 3500, 4.0f, 1.6f }, { 0, 8000, -2.0f, 1.0f } };
        x.driveDb = 4; x.excursion = 1.6f; x.exFreq = 500; x.limThr = -12; x.limRatio = 5; x.limAtt = 2; x.limRel = 100;
        x.room = 0.05f; x.rt = 0.25f; x.size = 0.2f; x.damp = 8000;
        x.listenFor = "Most of your listeners: is the drop still exciting without sub?"; add (x);

        x = {}; x.name = "TikTok / Reels on Phone"; x.shortName = "TIKTOK"; x.cat = Cat::Mobile; x.icon = IcReels;
        x.width = 0.45f; x.hp = 320; x.hpOrder = 4; x.lp = 15000; x.lpOrder = 4;
        x.eq = { { 0, 1200, 2.0f, 1.0f }, { 0, 3500, 4.0f, 1.6f }, { 0, 8000, -2.0f, 1.0f } };
        x.driveDb = 4; x.excursion = 1.6f; x.exFreq = 500; x.limThr = -12; x.limRatio = 5; x.limAtt = 2; x.limRel = 100;
        x.special = Stream; x.target = -14; x.boostAllowed = false;
        x.listenFor = "Short-video playback: normalised to about -14 LUFS on a phone.  The first 3 seconds must grab."; add (x);

        x = {}; x.name = "Cheap Earbuds"; x.shortName = "EARBUDS"; x.cat = Cat::Mobile; x.icon = IcEarbuds; x.speaker = false;
        x.hp = 70; x.hpOrder = 2; x.lp = 13000; x.lpOrder = 2;
        x.eq = { { 1, 120, -3.0f, 0.7f }, { 0, 3000, 6.0f, 1.2f }, { 0, 7200, 5.0f, 3.0f } };
        x.driveDb = 2; x.excursion = 0.5f; x.exFreq = 120;
        x.listenFor = "Harsh / sibilant highs and thin bass: are hats or vocals piercing?"; add (x);

        x = {}; x.name = "Premium Earbuds"; x.shortName = "AIRBUDS"; x.cat = Cat::Mobile; x.icon = IcPods; x.speaker = false;
        x.lp = 19000; x.lpOrder = 2;
        x.eq = { { 1, 80, 3.5f, 0.7f }, { 0, 2800, 2.0f, 1.0f }, { 0, 6000, -2.5f, 1.4f } };
        x.limThr = -3; x.limRatio = 3; x.limAtt = 5; x.limRel = 200;
        x.listenFor = "How most people hear it on the go: balanced, a little bass lift, very close stereo."; add (x);

        x = {}; x.name = "Bass Headphones"; x.shortName = "BASS PHONES"; x.cat = Cat::Mobile; x.icon = IcHeadphones; x.speaker = false;
        x.eq = { { 1, 90, 8.0f, 0.7f }, { 0, 220, 2.0f, 1.0f }, { 0, 2000, -3.0f, 0.8f }, { 2, 8500, 3.0f, 0.7f } };
        x.driveDb = 1; x.excursion = 0.4f; x.exFreq = 90;
        x.listenFor = "Consumer V-shape: does your low end turn into mud?"; add (x);

        x = {}; x.name = "Laptop Speakers"; x.shortName = "LAPTOP"; x.cat = Cat::Home; x.icon = IcLaptop;
        x.width = 0.35f; x.hp = 220; x.hpOrder = 4; x.lp = 14000; x.lpOrder = 4;
        x.eq = { { 0, 1000, 4.0f, 1.0f }, { 0, 4200, 3.0f, 1.2f }, { 0, 9000, -3.0f, 1.0f } };
        x.driveDb = 5; x.excursion = 1.8f; x.exFreq = 350; x.limThr = -12; x.limRatio = 5; x.limAtt = 2; x.limRel = 120;
        x.room = 0.08f; x.rt = 0.35f; x.size = 0.3f; x.damp = 6000;
        x.listenFor = "Small and boxy: is the vocal / lead too loud or too buried?"; add (x);

        x = {}; x.name = "Bluetooth Speaker"; x.shortName = "BT SPEAKER"; x.cat = Cat::Home; x.icon = IcBtSpeaker;
        x.width = 0.2f; x.hp = 62; x.hpOrder = 4; x.lp = 15000; x.lpOrder = 2;
        x.eq = { { 0, 92, 6.0f, 1.5f }, { 0, 380, -2.5f, 0.9f }, { 0, 3000, 3.0f, 1.0f } };
        x.driveDb = 3; x.excursion = 1.2f; x.exFreq = 120; x.limThr = -16; x.limRatio = 10; x.limAtt = 8; x.limRel = 350; x.limSc = 180;
        x.room = 0.14f; x.rt = 0.5f; x.size = 0.4f; x.predelay = 4; x.damp = 5000;
        x.listenFor = "The bass limiter pumps: does a loud sub make the whole track duck?"; add (x);

        x = {}; x.name = "TV Speakers"; x.shortName = "TV"; x.cat = Cat::Home; x.icon = IcTv;
        x.width = 0.6f; x.hp = 120; x.hpOrder = 4; x.lp = 12000; x.lpOrder = 2;
        x.eq = { { 0, 300, 2.0f, 1.0f }, { 0, 2500, 3.0f, 1.0f }, { 2, 7000, -6.0f, 0.7f } };
        x.driveDb = 2; x.excursion = 0.8f; x.exFreq = 200;
        x.room = 0.2f; x.rt = 0.5f; x.size = 0.5f; x.predelay = 6; x.damp = 4500; x.noise = Living; x.noiseDb = -42;
        x.listenFor = "Down-firing, dull: is the mix still clear without the top end?"; add (x);

        x = {}; x.name = "Kitchen Radio"; x.shortName = "KITCHEN"; x.cat = Cat::Home; x.icon = IcRadio;
        x.width = 0; x.hp = 150; x.hpOrder = 2; x.lp = 7000; x.lpOrder = 2;
        x.eq = { { 0, 260, 3.0f, 1.0f }, { 0, 1500, 3.0f, 0.8f } };
        x.driveDb = 6; x.excursion = 1.0f; x.exFreq = 250;
        x.room = 0.18f; x.rt = 0.6f; x.size = 0.4f; x.predelay = 5; x.damp = 3500; x.noise = Living; x.noiseDb = -40;
        x.listenFor = "Mono, mid-only: the classic 'does it still work' test.  Is the hook audible?"; add (x);

        x = {}; x.name = "FM Radio"; x.shortName = "FM RADIO"; x.cat = Cat::Broadcast; x.icon = IcFm;
        x.width = 0.9f; x.special = FM; x.noise = Hiss; x.noiseDb = -64;
        x.listenFor = "Broadcast processing squashes everything: do the drops still feel bigger than the breaks?"; add (x);

        x = {}; x.name = "AM Radio"; x.shortName = "AM RADIO"; x.cat = Cat::Broadcast; x.icon = IcAm;
        x.width = 0; x.special = AM; x.hp = 150; x.hpOrder = 4; x.lp = 4500; x.lpOrder = 8; x.noise = Crackle; x.noiseDb = -42;
        x.driveDb = 3;
        x.listenFor = "Extreme band-limit: only the core melody and rhythm survive."; add (x);

        x = {}; x.name = "Shop Ceiling Speakers"; x.shortName = "SHOP"; x.cat = Cat::Home; x.icon = IcCeiling;
        x.width = 0; x.hp = 140; x.hpOrder = 4; x.lp = 9000; x.lpOrder = 4;
        x.eq = { { 0, 1000, 3.0f, 0.9f } };
        x.driveDb = 2; x.room = 0.38f; x.rt = 1.2f; x.size = 0.8f; x.predelay = 12; x.damp = 3500; x.noise = Street; x.noiseDb = -34;
        x.listenFor = "Background music: does the groove still come across in a reverberant shop?"; add (x);

        x = {}; x.name = "Next Room (Through the Wall)"; x.shortName = "NEXT ROOM"; x.cat = Cat::Home; x.icon = IcWall;
        x.width = 0.3f; x.lp = 260; x.lpOrder = 4; x.eq = { { 1, 60, 3.0f, 0.7f } };
        x.room = 0.4f; x.rt = 0.8f; x.size = 0.6f; x.predelay = 8; x.damp = 1500;
        x.listenFor = "The party next door: only kick, bass and the groove remain - is it still danceable?"; add (x);

        x = {}; x.name = "MixCube (Single Mid Speaker)"; x.shortName = "MIXCUBE"; x.cat = Cat::Studio; x.icon = IcCube;
        x.width = 0; x.hp = 90; x.hpOrder = 4; x.lp = 13000; x.lpOrder = 2;
        x.eq = { { 0, 1500, 2.5f, 0.7f } };
        x.driveDb = 1;
        x.listenFor = "The engineer's grot-box: balance of vocal / lead / snare without the hype."; add (x);

        x = {}; x.name = "Nearfield (NS-Style)"; x.shortName = "NEARFIELD"; x.cat = Cat::Studio; x.icon = IcNearfield;
        x.hp = 62; x.hpOrder = 2;
        x.eq = { { 0, 1800, 3.0f, 0.9f }, { 2, 8000, -2.0f, 0.7f } };
        x.room = 0.06f; x.rt = 0.3f; x.size = 0.3f; x.predelay = 3; x.damp = 7000;
        x.listenFor = "Mid-forward and unflattering: if it sounds good here, it sounds good anywhere."; add (x);

        x = {}; x.name = "Hi-Fi Living Room"; x.shortName = "HI-FI"; x.cat = Cat::Home; x.icon = IcHifi;
        x.eq = { { 1, 60, 2.0f, 0.7f }, { 0, 180, 1.5f, 1.2f }, { 2, 10000, 1.5f, 0.7f } };
        x.room = 0.16f; x.rt = 0.45f; x.size = 0.5f; x.predelay = 6; x.damp = 6000;
        x.listenFor = "Full-range at home: is it too bright or too heavy for long listening?"; add (x);

        x = {}; x.name = "Spotify (-14 LUFS)"; x.shortName = "SPOTIFY"; x.cat = Cat::Streaming; x.icon = IcStream; x.speaker = false;
        x.special = Stream; x.target = -14; x.boostAllowed = true; x.lp = 19500; x.lpOrder = 4;
        x.listenFor = "Loudness normalised: is your loud master turned down and now sounds smaller than others?"; add (x);

        x = {}; x.name = "YouTube (-14 LUFS)"; x.shortName = "YOUTUBE"; x.cat = Cat::Streaming; x.icon = IcStream; x.speaker = false;
        x.special = Stream; x.target = -14; x.boostAllowed = false; x.lp = 16000; x.lpOrder = 4;
        x.listenFor = "YouTube only turns DOWN: an over-compressed master just loses its punch."; add (x);

        x = {}; x.name = "Apple Music (-16 LUFS)"; x.shortName = "APPLE"; x.cat = Cat::Streaming; x.icon = IcStream; x.speaker = false;
        x.special = Stream; x.target = -16; x.boostAllowed = true;
        x.listenFor = "Sound Check at -16 LUFS: dynamic masters win here."; add (x);

        x = {}; x.name = "SoundCloud"; x.shortName = "SOUNDCLOUD"; x.cat = Cat::Streaming; x.icon = IcStream; x.speaker = false;
        x.special = Stream; x.target = 0; x.boostAllowed = false; x.lp = 16000; x.lpOrder = 4;
        x.listenFor = "No normalisation, 128 kbps-style top end: loud masters stay loud."; add (x);

        x = {}; x.name = "Custom IR"; x.shortName = "CUSTOM IR"; x.cat = Cat::Custom; x.icon = IcIr; x.speaker = false;
        x.special = Custom;
        x.listenFor = "Load any impulse response (your car, your club, a friend's speaker) and hear the mix through it."; add (x);
        return v;
    }();
    return d;
}

inline int numDevices() { return (int) devices().size(); }
inline int deviceIndex (const char* shortName)
{
    for (int i = 0; i < numDevices(); ++i) if (juce::String (devices()[(size_t) i].shortName) == shortName) return i;
    return 0;
}
}
