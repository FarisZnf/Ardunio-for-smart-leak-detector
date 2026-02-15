// --- Definisi Pin Flowmeter ---
const byte FLOWMETER1_PIN = 2; // Pin untuk Flowmeter 1 (YF-S201)
const byte FLOWMETER2_PIN = 3; // Pin untuk Flowmeter 2 (YF-S201)
const byte FLOWMETER3_PIN = 4; // Pin untuk Flowmeter 3 (FS300A G3/4")

// --- Definisi Pin Buzzer ---
const byte BUZZER_PIN = 5;     // Pin untuk Buzzer

// --- Faktor Kalibrasi (Pulsa per Liter) ---
const float CALIBRATION_FACTOR_FM1 = 450.0;
const float CALIBRATION_FACTOR_FM2 = 692.0;
const float CALIBRATION_FACTOR_FM3 = 323.0;

// --- [BARU] Thresholds untuk Deteksi Kebocoran ---
// Disesuaikan berdasarkan analisis data terbaru.
const float ZERO_FLOW_LIMIT = 0.1;   // Batas atas untuk dianggap tidak ada aliran.
const float LEAK_BOTH_FM3_ZERO_LIMIT = 0.1;   // Batas untuk menganggap FM3 = 0 pada kebocoran ganda.
const float LEAK12_DROP_THRESHOLD = 2.0;   // Penurunan besar dari FM1 ke FM2 untuk kebocoran segmen 1.

// --- Variabel untuk Menyimpan Jumlah Pulsa ---
volatile unsigned long pulseCount1 = 0;
volatile unsigned long pulseCount2 = 0;
volatile unsigned long pulseCount3 = 0;

// --- Variabel untuk Menyimpan Laju Alir ---
float flowRate1 = 0.0;
float flowRate2 = 0.0;
float flowRate3 = 0.0;
float totalFlowRate = 0.0;

// --- Variabel untuk Pengaturan Waktu ---
unsigned long previousMillis = 0;
const long interval = 1000;
volatile byte lastFlowmeter3PinState;

// --- ISR (Interrupt Service Routines) ---
void countPulse1() { pulseCount1++; }
void countPulse2() { pulseCount2++; }
ISR(PCINT2_vect) {
  byte currentFlowmeter3PinState = digitalRead(FLOWMETER3_PIN);
  if (currentFlowmeter3PinState == LOW && lastFlowmeter3PinState == HIGH) {
    pulseCount3++;
  }
  lastFlowmeter3PinState = currentFlowmeter3PinState;
}

void setup() {
  Serial.begin(9600);
  Serial.println("Sistem Deteksi Kebocoran Pipa - Logika Baru");

  pinMode(FLOWMETER1_PIN, INPUT_PULLUP);
  pinMode(FLOWMETER2_PIN, INPUT_PULLUP);
  pinMode(FLOWMETER3_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  attachInterrupt(digitalPinToInterrupt(FLOWMETER1_PIN), countPulse1, FALLING);
  attachInterrupt(digitalPinToInterrupt(FLOWMETER2_PIN), countPulse2, FALLING);

  PCICR |= (1 << PCIE2);
  PCMSK2 |= (1 << PCINT20);
  lastFlowmeter3PinState = digitalRead(FLOWMETER3_PIN);

  interrupts();
  
  Serial.println("Format Output: FM1, FM2, FM3, Total, Buzzer, Kondisi");
  Serial.println("----------------------------------------------------");
  previousMillis = millis();
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Bagian kalkulasi flowrate tetap sama
    noInterrupts();
    unsigned long currentPulse1 = pulseCount1;
    unsigned long currentPulse2 = pulseCount2;
    unsigned long currentPulse3 = pulseCount3;
    pulseCount1 = 0; pulseCount2 = 0; pulseCount3 = 0;
    interrupts();
    flowRate1 = (float)currentPulse1 / CALIBRATION_FACTOR_FM1 * (60000.0 / interval);
    flowRate2 = (float)currentPulse2 / CALIBRATION_FACTOR_FM2 * (60000.0 / interval);
    flowRate3 = (float)currentPulse3 / CALIBRATION_FACTOR_FM3 * (60000.0 / interval);
    totalFlowRate = flowRate1 + flowRate2 + flowRate3;

    // --- [LOGIKA DETEKSI BARU] ---
    String kondisiDeteksi = "Normal";
    bool buzzerShouldBeOn = false;

    // Definisikan kondisi berdasarkan pola data terbaru
    bool isZeroFlowNormal = (flowRate1 < ZERO_FLOW_LIMIT) && (flowRate2 < ZERO_FLOW_LIMIT) && (flowRate3 < ZERO_FLOW_LIMIT);
    bool isFlowingNormal = (flowRate1 > flowRate2) && (flowRate3 > flowRate2);
    bool isLeakBoth = (flowRate3 < LEAK_BOTH_FM3_ZERO_LIMIT) && (flowRate1 > 1.0);
    bool isLeak12 = (flowRate1 - flowRate2 > LEAK12_DROP_THRESHOLD) && (flowRate3 > flowRate2);
    bool isLeak23 = (flowRate1 > flowRate2) && (flowRate2 > flowRate3);

    // Urutan pengecekan (if-else if) penting untuk menghindari salah deteksi
    if (isZeroFlowNormal) {
      buzzerShouldBeOn = false;
      kondisiDeteksi = "Normal (Air Off)";
    } else if (isLeakBoth) {
      // Cek kebocoran ganda lebih awal karena cirinya paling khas (FM3=0)
      buzzerShouldBeOn = true;
      kondisiDeteksi = "Bocor Ganda (Segmen 1 & 2)";
    } else if (isLeak12) {
      // Cek bocor segmen 1 berikutnya karena polanya juga unik (FM3 > FM2)
      buzzerShouldBeOn = true;
      kondisiDeteksi = "Bocor di Segmen 1";
    } else if (isLeak23) {
      // Cek bocor segmen 2, yaitu pola penurunan berurutan
      buzzerShouldBeOn = true;
      kondisiDeteksi = "Bocor di Segmen 2";
    } else if (isFlowingNormal) {
      // Jika tidak ada pola bocor yang cocok, baru cek apakah polanya normal
      buzzerShouldBeOn = false;
      kondisiDeteksi = "Normal";
    } else {
      // Jika tidak ada pola yang cocok sama sekali, anggap sebagai anomali/kebocoran
      buzzerShouldBeOn = true;
      kondisiDeteksi = "Anomali Tidak Dikenali";
    }

    // Atur status Buzzer
    digitalWrite(BUZZER_PIN, buzzerShouldBeOn);

    // Tampilkan data ke Serial Monitor
    Serial.print(flowRate1, 2); Serial.print(", ");
    Serial.print(flowRate2, 2); Serial.print(", ");
    Serial.print(flowRate3, 2); Serial.print(", Total: ");
    Serial.print(totalFlowRate, 2); Serial.print(", Buzzer: ");
    Serial.print(digitalRead(BUZZER_PIN) == HIGH ? "ON" : "OFF");
    Serial.print(", Kondisi: ");
    Serial.println(kondisiDeteksi);
  }
}