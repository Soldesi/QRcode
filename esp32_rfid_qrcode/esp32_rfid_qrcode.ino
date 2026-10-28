/*
  ESP32 + RFID (MFRC522) + OLED SSD1306 (QR code) + LEDs + Buzzer + Servo

  Bibliotecas (Gerenciador de Bibliotecas da Arduino IDE):
    - MFRC522               (GithubCommunity)
    - Adafruit SSD1306
    - Adafruit GFX Library
    - ESP32Servo            (Kevin Harrington)

  Pinagem:
    OLED SDA / SCL ........ 21 / 22
    RFID SS ............... 26
    RFID SCK/MISO/MOSI .... 18 / 19 / 23
    RFID RST .............. 27
    LED verde ............. 32
    LED vermelho .......... 33
    Buzzer ................ 25
    Servo (sinal) ......... 13
*/

#include <SPI.h>
#include <Wire.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "qrcodegen.h"   // arquivo local (pasta do sketch)
#include <ESP32Servo.h>

// ---------- PINOS ----------
#define RFID_SS    26
#define RFID_RST   27
#define SPI_SCK    18
#define SPI_MISO   19
#define SPI_MOSI   23

#define OLED_SDA   21
#define OLED_SCL   22

#define LED_VERDE    32
#define LED_VERMELHO 33
#define BUZZER       25
#define SERVO_PIN    13

// ---------- OLED ----------
#define OLED_LARGURA 128
#define OLED_ALTURA  64
#define OLED_ENDERECO 0x3C   // se nao funcionar, tente 0x3D

Adafruit_SSD1306 display(OLED_LARGURA, OLED_ALTURA, &Wire, -1);

// ---------- QR CODE ----------
// DICA: o OLED e bicolor (16 linhas de cima AMARELAS, o resto AZUL).
// Para o celular ler bem, o QR precisa ficar todo na parte azul (48 px de altura).
// Isso so cabe se o QR for pequeno (versao 1 = ate 17 caracteres, ou
// ate 25 caracteres se o texto for TODO EM MAIUSCULAS e sem acentos).
// Exemplo: "GITHUB.COM/SOLDESI" (sites nao diferenciam maiuscula/minuscula)
const char* QR_TEXTO = "GITHUB.COM/SOLDESI";

#define QR_VERSAO_MIN 1
#define QR_VERSAO_MAX 3
#define QR_ESCALA 2          // cada modulo = 2x2 pixels

uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSAO_MAX)];
uint8_t qrTemp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSAO_MAX)];
bool qrOk = false;

// ---------- RFID ----------
MFRC522 rfid(RFID_SS, RFID_RST);

// UIDs autorizados (use o UID que aparece no Serial Monitor)
const char* UIDS_AUTORIZADOS[] = {
  "AA:BB:CC:DD",
  "11:22:33:44"
};
const int TOTAL_AUTORIZADOS = sizeof(UIDS_AUTORIZADOS) / sizeof(UIDS_AUTORIZADOS[0]);

// ---------- SERVO ----------
Servo servo;
#define SERVO_FECHADO 0
#define SERVO_ABERTO  90
#define TEMPO_ABERTO  2000   // ms que o servo fica aberto

// ---------- FUNCOES ----------
void beep(int duracao) {
  digitalWrite(BUZZER, HIGH);
  delay(duracao);
  digitalWrite(BUZZER, LOW);
}

void desenharQRCode() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  int tamanho = qrOk ? qrcodegen_getSize(qrcode) : 0;
  int pixels = tamanho * QR_ESCALA;

  if (tamanho > 0 && pixels + 6 <= 48) {
    // QR pequeno: fica inteiro na parte AZUL (y = 16 a 63)
    display.fillRect(0, 16, 48, 48, SSD1306_WHITE);        // fundo claro
    int ox = (48 - pixels) / 2;
    int oy = 16 + (48 - pixels) / 2;
    for (int y = 0; y < tamanho; y++)
      for (int x = 0; x < tamanho; x++)
        if (qrcodegen_getModule(qrcode, x, y))
          display.fillRect(ox + x * QR_ESCALA, oy + y * QR_ESCALA,
                           QR_ESCALA, QR_ESCALA, SSD1306_BLACK);

    // Titulo na faixa amarela e texto ao lado do QR
    display.setCursor(0, 4);
    display.println("Aproxime o cartao");
    display.setCursor(56, 30);
    display.println("ou leia o");
    display.setCursor(56, 42);
    display.println("QR code");
  } else {
    // QR grande (texto longo): ocupa a altura toda (atravessa as 2 cores)
    display.fillRect(0, 0, 64, 64, SSD1306_WHITE);
    int off = (64 - pixels) / 2;
    for (int y = 0; y < tamanho; y++)
      for (int x = 0; x < tamanho; x++)
        if (qrcodegen_getModule(qrcode, x, y))
          display.fillRect(off + x * QR_ESCALA, off + y * QR_ESCALA,
                           QR_ESCALA, QR_ESCALA, SSD1306_BLACK);
    display.setCursor(70, 14);
    display.println("Aproxime");
    display.setCursor(70, 26);
    display.println("o cartao");
    display.setCursor(70, 44);
    display.println("ou leia o");
    display.setCursor(70, 54);
    display.println("QR code");
  }

  display.display();
}

void mostrarMensagem(const char* titulo, const String& linha2) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 8);
  display.println(titulo);
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.println("UID:");
  display.setCursor(0, 52);
  display.println(linha2);
  display.display();
}

String lerUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uid += ":";
  }
  uid.toUpperCase();
  return uid;
}

bool uidAutorizado(const String& uid) {
  for (int i = 0; i < TOTAL_AUTORIZADOS; i++) {
    if (uid.equalsIgnoreCase(UIDS_AUTORIZADOS[i])) return true;
  }
  return false;
}

void acessoLiberado(const String& uid) {
  mostrarMensagem("LIBERADO", uid);
  digitalWrite(LED_VERDE, HIGH);
  beep(150);
  servo.write(SERVO_ABERTO);
  delay(TEMPO_ABERTO);
  servo.write(SERVO_FECHADO);
  digitalWrite(LED_VERDE, LOW);
}

void acessoNegado(const String& uid) {
  mostrarMensagem("NEGADO", uid);
  digitalWrite(LED_VERMELHO, HIGH);
  beep(100);
  delay(100);
  beep(100);
  delay(100);
  beep(100);
  delay(800);
  digitalWrite(LED_VERMELHO, LOW);
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("ESP32 INICIANDO...");

  // Saidas
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, LOW);
  digitalWrite(BUZZER, LOW);

  // Servo
  servo.setPeriodHertz(50);
  servo.attach(SERVO_PIN, 500, 2400);
  servo.write(SERVO_FECHADO);

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ENDERECO)) {
    Serial.println("Falha ao iniciar o OLED! Verifique SDA/SCL e endereco.");
  }

  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(90);   // brilho menor = QR mais nitido na camera

  // QR code
  qrOk = qrcodegen_encodeText(QR_TEXTO, qrTemp, qrcode, qrcodegen_Ecc_LOW,
                              QR_VERSAO_MIN, QR_VERSAO_MAX, qrcodegen_Mask_AUTO, true);
  if (!qrOk) Serial.println("QR code: texto muito longo (maximo versao 3)!");

  // RFID
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, RFID_SS);
  rfid.PCD_Init();
  delay(100);

  Serial.println("Leitor RFID iniciado!");
  Serial.println("Aproxime o cartao ou chaveiro...");

  // Teste rapido dos atuadores
  digitalWrite(LED_VERDE, HIGH);
  digitalWrite(LED_VERMELHO, HIGH);
  beep(100);
  delay(200);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, LOW);

  desenharQRCode();
}

// ---------- LOOP ----------
void loop() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uid = lerUID();
  Serial.print("UID do cartao: ");
  Serial.println(uid);

  if (uidAutorizado(uid)) {
    Serial.println("Acesso LIBERADO");
    acessoLiberado(uid);
  } else {
    Serial.println("Acesso NEGADO");
    acessoNegado(uid);
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(500);
  desenharQRCode();   // volta para a tela do QR code
  Serial.println("Aproxime outro cartao...");
}