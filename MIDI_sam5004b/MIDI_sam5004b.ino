#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <HardwareSerial.h>
#include <MIDI.h>

// ====== Hardware Configuration ======
#define MIDI_TX_PIN 17
#define PRESSURE_DEBUG true
#define IMU_DEBUG false
#define BLE_DEBUG true

// ====== MIDI Configuration ======
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, MIDI);
const int PRESSURE_THRESHOLD = 50;
const int CHORD_ANGLE_THRESHOLD = 50;

// Arpeggio parameters
unsigned long ARPEGGIO_DELAY = 200;   // Note interval (ms)
unsigned long lastArpeggioTime = 0;
int currentArpeggioStep = 0;          // Current step in arpeggio sequence

// ====== MIDI Note Definitions ======
// Octave 0 (C-1 ~ B-1)
#define C_1 0
#define Cs_1 1
// ...other low-range notes...

// Octave 3 (C3 ~ B3)
#define C3 48
#define Cs3 49
#define D3 50
#define Ds3 51
#define E3 52
#define F3 53
#define Fs3 54
#define G3 55
#define Gs3 56
#define A3 57
#define As3 58
#define B3 59

// Octave 4 (C4 ~ B4) - primary octave used
#define C4 60
#define Cs4 61
#define D4 62
#define Ds4 63
#define E4 64
#define F4 65
#define Fs4 66
#define G4 67
#define Gs4 68
#define A4 69
#define As4 70
#define B4 71

// Octave 5 (C5 ~ B5)
#define C5  72
#define Cs5 73
#define D5  74
#define Ds5 75
#define E5  76
#define F5  77
#define Fs5 78
#define G5  79
#define Gs5 80
#define A5  81
#define As5 82
#define B5  83
// ...higher notes...

// ====== Sensor-to-Note Mapping ======
const byte NOTE_MAPPING[] = {
  C4, D4, E4, F4, G4, A4, B4
};

// ====== Custom Chord Definitions ======
const std::vector<byte> CUSTOM_CHORDS[] = {
  /* Sensors 1-5 */
  {C4, G4, C5, E5},   // C major
  {D3, A3, D4, F4},   // D minor
  {E3, B3, E4, G4},   // E major
  {F3, C4, F4, A4},   // F major
  {G3, D4, G4, B4},   // G major

  /* Combo keys */
  {A3, E4, A4, C5},   // A major
  {B3, F4, B4, D5}    // B major
};

// ====== Chord Configuration ======
enum ChordType {
  MAJOR,
  MINOR,
  AUGMENTED,
  DIMINISHED,
  SUS4,
  SEVENTH
};

struct Chord {
  byte rootNote;
  byte thirdNote;
  byte fifthNote;
  byte seventhNote;
  ChordType type;
  bool active;
  unsigned long lastPressTime;
  bool notesPlayed[4];     // Track chord note playback status
};

const unsigned long CHORD_PLAY_DELAY = 500; // Delay between chord notes (ms)
unsigned long lastChordPlayTime = 0;
Chord currentChord = {0, 0, 0, 0, MAJOR, false, 0, {false}};

// ====== Combo Key Configuration ======
const int COMBO_KEY = 15;
const int A4_KEY = 1;
const int B4_KEY = 2;
bool comboKeyActive = false;

// ====== BLE Configuration ======
const char* TARGET_DEVICE_NAME = "ESP32_IMU_Server";
BLEUUID SERVICE_UUID("12345678-1234-1234-1234-123456789ABC");
BLEUUID CHAR_UUID("ABCDEF12-3456-7890-1234-567890ABCDEF");

// ====== Controller Module ======
class MidiController {
private:
  uint16_t pressureData[25] = {0};
  bool notesActive[7] = {false};
  float currentRoll = 0;
  bool sustainActive = false;

  void debugPrint(const String &message) {
    // if (BLE_DEBUG) Serial.println(message);
  }

public:
  void updateSensorData(float roll, uint16_t* pressures) {
    currentRoll = roll;
    memcpy(pressureData, pressures, sizeof(uint16_t) * 25);

    if (PRESSURE_DEBUG) {
      debugPrint("=== Sensor Data ===");
      debugPrint("Combo Key (" + String(COMBO_KEY) + "): " + String(pressureData[COMBO_KEY]));
      debugPrint("A4 Key (" + String(A4_KEY) + "): " + String(pressureData[A4_KEY]));
      debugPrint("B4 Key (" + String(B4_KEY) + "): " + String(pressureData[B4_KEY]));

      for (int i = 0; i < 25; i++) {
        if (pressureData[i] > 10) {
          debugPrint("Sensor " + String(i) + ": " + String(pressureData[i]));
        }
      }
    }
  }

  void handleControls() {
    checkComboKeys();
    handleChordPlayback();
    checkSustain();
  }

  void allNotesOff() {
    for (int i = 0; i < 7; i++) {
      if (notesActive[i]) {
        MIDI.sendNoteOff(NOTE_MAPPING[i], 0, 1);
        notesActive[i] = false;
      }
    }

    if (sustainActive) {
      MIDI.sendControlChange(64, 0, 1);
      sustainActive = false;
    }

    stopCurrentChord();
    debugPrint("All notes turned off");
  }

  void setChordType(ChordType type) {
    currentChord.type = type;
    updateChordNotes();
  }

private:
  void checkComboKeys() {
    bool newComboState = (pressureData[COMBO_KEY] > PRESSURE_THRESHOLD);

    if (newComboState != comboKeyActive) {
      comboKeyActive = newComboState;
      if (!comboKeyActive) {
        releaseNote(5); // A4
        releaseNote(6); // B4
        debugPrint("Combo key released");
      }
    }

    if (comboKeyActive) {
      handleComboNotes();
    } else {
      handleSingleNotes();
    }
  }

  void handleComboNotes() {
    // A4 trigger
    if (pressureData[A4_KEY] > PRESSURE_THRESHOLD) {
      int velocity = calculateVelocity(A4_KEY);

      if (!currentChord.active || currentChord.rootNote != NOTE_MAPPING[5]) {
        stopCurrentChord();
        currentChord.rootNote = NOTE_MAPPING[5]; // A4
        updateChordNotes();
        currentChord.lastPressTime = millis();
        currentChord.active = true;
        memset(currentChord.notesPlayed, false, sizeof(currentChord.notesPlayed));
      }

      playNote(5, velocity, "A4");
      if (notesActive[1]) releaseNote(1); // Force off D4
    }
    // B4 trigger
    else if (pressureData[B4_KEY] > PRESSURE_THRESHOLD) {
      int velocity = calculateVelocity(B4_KEY);

      if (!currentChord.active || currentChord.rootNote != NOTE_MAPPING[6]) {
        stopCurrentChord();
        currentChord.rootNote = NOTE_MAPPING[6]; // B4
        updateChordNotes();
        currentChord.lastPressTime = millis();
        currentChord.active = true;
        memset(currentChord.notesPlayed, false, sizeof(currentChord.notesPlayed));
      }

      playNote(6, velocity, "B4");
      if (notesActive[2]) releaseNote(2); // Force off E4
    }
    else {
      releaseNote(5);
      releaseNote(6);
      if (currentChord.active &&
          (currentChord.rootNote == NOTE_MAPPING[5] || currentChord.rootNote == NOTE_MAPPING[6])) {
        stopCurrentChord();
      }
    }
  }

  void handleSingleNotes() {
    for (int i = 0; i < 5; i++) {
      if (pressureData[i] > PRESSURE_THRESHOLD) {
        int velocity = calculateVelocity(i);

        if (!currentChord.active || currentChord.rootNote != NOTE_MAPPING[i]) {
          stopCurrentChord();
          currentChord.rootNote = NOTE_MAPPING[i];
          updateChordNotes();
          currentChord.lastPressTime = millis();
          currentChord.active = true;
          memset(currentChord.notesPlayed, false, sizeof(currentChord.notesPlayed));
        }

        playNote(i, velocity, "Key " + String(NOTE_MAPPING[i]));
      }
      else if (notesActive[i]) {
        releaseNote(i);
        if (currentChord.active && currentChord.rootNote == NOTE_MAPPING[i]) {
          stopCurrentChord();
        }
      }
    }
  }

  void updateChordNotes() {
    switch (currentChord.type) {
      case MAJOR:
        currentChord.thirdNote = currentChord.rootNote + 4;
        currentChord.fifthNote = currentChord.rootNote + 7;
        currentChord.seventhNote = 0; // Not used
        break;
      case MINOR:
        currentChord.thirdNote = currentChord.rootNote + 3;
        currentChord.fifthNote = currentChord.rootNote + 7;
        currentChord.seventhNote = 0;
        break;
      case AUGMENTED:
        currentChord.thirdNote = currentChord.rootNote + 4;
        currentChord.fifthNote = currentChord.rootNote + 8;
        currentChord.seventhNote = 0;
        break;
      case DIMINISHED:
        currentChord.thirdNote = currentChord.rootNote + 3;
        currentChord.fifthNote = currentChord.rootNote + 6;
        currentChord.seventhNote = 0;
        break;
      case SUS4:
        currentChord.thirdNote = currentChord.rootNote + 5;
        currentChord.fifthNote = currentChord.rootNote + 7;
        currentChord.seventhNote = 0;
        break;
      case SEVENTH:
        currentChord.thirdNote = currentChord.rootNote + 4;
        currentChord.fifthNote = currentChord.rootNote + 7;
        currentChord.seventhNote = currentChord.rootNote + 10;
        break;
    }

    debugPrint("Chord type: " + String((int)currentChord.type) +
               " Notes: " + String(currentChord.rootNote) + "," +
               String(currentChord.thirdNote) + "," +
               String(currentChord.fifthNote));
  }

  void handleChordPlayback() {
    if (!currentChord.active || abs(currentRoll) <= CHORD_ANGLE_THRESHOLD) {
      currentArpeggioStep = 0; // Reset arpeggio step
      return;
    }

    // Determine currently active chord index
    int chordIndex = -1;
    if (comboKeyActive) {
      if (pressureData[A4_KEY] > PRESSURE_THRESHOLD) chordIndex = 5;      // A
      else if (pressureData[B4_KEY] > PRESSURE_THRESHOLD) chordIndex = 6; // B
    } else {
      for (int i = 0; i < 5; i++) {
        if (pressureData[i] > PRESSURE_THRESHOLD) {
          chordIndex = i;
          break;
        }
      }
    }

    if (chordIndex < 0) {
      stopCurrentChord();
      return;
    }

    // Arpeggiated chord playback
    auto& chord = CUSTOM_CHORDS[chordIndex];
    if (millis() - lastArpeggioTime > ARPEGGIO_DELAY) {
      // Turn off previous note
      if (currentArpeggioStep > 0) {
        MIDI.sendNoteOff(chord[currentArpeggioStep - 1], 0, 1);
      }

      // Play current note
      MIDI.sendNoteOn(chord[currentArpeggioStep], 100, 1);
      debugPrint("Arpeggio step " + String(currentArpeggioStep) +
                 ": note " + String(chord[currentArpeggioStep]));

      // Advance to next note (loop)
      currentArpeggioStep = (currentArpeggioStep + 1) % chord.size();
      lastArpeggioTime = millis();
    }
  }

  void checkSustain() {
    if (!currentChord.active) {
      if (sustainActive) {
        MIDI.sendControlChange(64, 0, 1);
        sustainActive = false;
      }
      return;
    }

    bool shouldSustain = (millis() - currentChord.lastPressTime > 300);

    if (shouldSustain != sustainActive) {
      MIDI.sendControlChange(64, shouldSustain ? 127 : 0, 1);
      sustainActive = shouldSustain;
      debugPrint("Sustain: " + String(shouldSustain ? "ON" : "OFF"));
    }
  }

  void stopCurrentChord() {
    if (currentChord.active) {
      int chordIndex = -1;

      // Identify last active chord
      if (comboKeyActive) {
        if (pressureData[A4_KEY] > 0) chordIndex = 5;
        else if (pressureData[B4_KEY] > 0) chordIndex = 6;
      } else {
        for (int i = 0; i < 5; i++) {
          if (pressureData[i] > 0) chordIndex = i;
        }
      }

      // Turn off all chord notes
      if (chordIndex >= 0) {
        auto& chord = CUSTOM_CHORDS[chordIndex];
        for (size_t i = 0; i < chord.size(); i++) {
          MIDI.sendNoteOff(chord[i], 0, 1);
        }
      }

      currentArpeggioStep = 0;
      currentChord.active = false;
      debugPrint("Arpeggio stopped");
    }
  }

  int getNoteIndex(byte note) {
    for (int i = 0; i < 7; i++) {
      if (NOTE_MAPPING[i] == note) return i;
    }
    return -1;
  }

  int calculateVelocity(int sensorIndex) {
    return map(min((int)pressureData[sensorIndex], 1000),
               PRESSURE_THRESHOLD, 1000, 70, 127);
  }

  void playNote(int noteIndex, int velocity, const String &noteName) {
    if (!notesActive[noteIndex]) {
      MIDI.sendNoteOn(NOTE_MAPPING[noteIndex], velocity, 1);
      notesActive[noteIndex] = true;
      debugPrint(noteName + " ON, velocity: " + String(velocity));
    }
  }

  void releaseNote(int noteIndex) {
    if (notesActive[noteIndex]) {
      MIDI.sendNoteOff(NOTE_MAPPING[noteIndex], 0, 1);
      notesActive[noteIndex] = false;
    }
  }
};

// ====== BLE Module ======
class BleManager {
private:
  BLEClient* pClient = nullptr;
  BLERemoteCharacteristic* pRemoteChar = nullptr;
  bool deviceConnected = false;
  std::string serverAddress;
  MidiController& controller;

  class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    BleManager* manager;
  public:
    MyAdvertisedDeviceCallbacks(BleManager* m) : manager(m) {}

    void onResult(BLEAdvertisedDevice advertisedDevice) {
      if (advertisedDevice.getName() == TARGET_DEVICE_NAME &&
          advertisedDevice.getServiceUUID().equals(SERVICE_UUID)) {
        manager->serverAddress = advertisedDevice.getAddress().toString();
        BLEDevice::getScan()->stop();
        Serial.println("Target device found: " + String(manager->serverAddress.c_str()));
      }
    }
  };

  class MyClientCallback : public BLEClientCallbacks {
    BleManager* manager;
  public:
    MyClientCallback(BleManager* m) : manager(m) {}

    void onConnect(BLEClient* pclient) {
      manager->deviceConnected = true;
      pclient->setMTU(128);
      Serial.println("Device connected");
    }

    void onDisconnect(BLEClient* pclient) {
      manager->deviceConnected = false;
      manager->controller.allNotesOff();
      Serial.println("Device disconnected");
    }
  };

public:
  BleManager(MidiController& ctrl) : controller(ctrl) {}

  void setup() {
    BLEDevice::init("ESP32_MIDI_Client");
    BLEScan* pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(this));
    pScan->setActiveScan(true);
    pScan->start(5);
    Serial.println("Start scanning for BLE devices...");
  }

  void loop() {
    if (!deviceConnected) {
      tryConnect();
    }
  }

private:
  void tryConnect() {
    if (!serverAddress.empty()) {
      pClient = BLEDevice::createClient();
      pClient->setClientCallbacks(new MyClientCallback(this));

      Serial.println("Attempting to connect...");
      if (pClient->connect(BLEAddress(serverAddress))) {
        setupNotifications();
      }
    } else {
      BLEDevice::getScan()->start(5);
    }
  }

  void setupNotifications() {
    BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
    if (!pRemoteService) {
      Serial.println("Service not found");
      return;
    }

    pRemoteChar = pRemoteService->getCharacteristic(CHAR_UUID);
    if (!pRemoteChar || !pRemoteChar->canNotify()) {
      Serial.println("Characteristic not found or notify not supported");
      return;
    }

    pRemoteChar->registerForNotify(
      [this](BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
        if (length < 66 || pData[0] != 0xAB || pData[1] != 0xCD) return;

        float roll = *((float*)&pData[6]);

        uint16_t pressures[25];
        for (int i = 0; i < 25; i++) {
          pressures[i] = (pData[14 + i * 2] << 8) | pData[15 + i * 2];
        }

        this->controller.updateSensorData(roll, pressures);
        this->controller.handleControls();
      }
    );

    uint8_t notifyOn[] = {0x01, 0x00};
    pRemoteChar->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue(notifyOn, 2, true);
    deviceConnected = true;
    Serial.println("Notifications enabled");
  }
};

// ====== Global Instances ======
MidiController controller;
BleManager bleManager(controller);

void setup() {
  Serial.begin(115200);

  MIDI.begin(1);
  MIDI.sendProgramChange(0, 1); // Set instrument/patch

  // Send a test note to verify MIDI output
  MIDI.sendNoteOn(71, 127, 1); // B4
  delay(200);
  MIDI.sendNoteOff(71, 0, 1);

  bleManager.setup();
  Serial.println("System initialized");
}

void loop() {
  bleManager.loop();

  // Serial command handling
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "c") {
      controller.allNotesOff();
    }
    else if (cmd == "major") {
      controller.setChordType(MAJOR);
    }
    else if (cmd == "minor") {
      controller.setChordType(MINOR);
    }
    else if (cmd == "aug") {
      controller.setChordType(AUGMENTED);
    }
    else if (cmd == "dim") {
      controller.setChordType(DIMINISHED);
    }
    else if (cmd == "sus4") {
      controller.setChordType(SUS4);
    }
    else if (cmd == "7") {
      controller.setChordType(SEVENTH);
    }
    else if (cmd.startsWith("arp ")) {
      ARPEGGIO_DELAY = cmd.substring(4).toInt();
      Serial.println("Arpeggio delay set to: " + String(ARPEGGIO_DELAY) + " ms");
    }
  }

  delay(10);
}
