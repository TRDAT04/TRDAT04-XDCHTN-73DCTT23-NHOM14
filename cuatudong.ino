#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <EEPROM.h>

// Định nghĩa các chân cho Keypad
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
byte rowPins[ROWS] = {A0, A1, A2, A3};
byte colPins[COLS] = {A4, A5, A6, A7};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Đối tượng LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Định nghĩa các chân và hằng số
#define RST_PIN         49
#define SS_PIN          53
#define MAX_UIDS        10
#define UID_LENGTH      4
#define BUZZER_PIN      6
#define LED_GREEN_PIN   A10
#define SERVO_PIN       29
#define PIR_PIN         31
#define BUTTON_PIN      39

// EEPROM addresses
#define EEPROM_START_ADDRESS 0
#define UID_TOTAL_ADDRESS 100
#define PASSWORD_ADDRESS 150

// Mã bí mật để đổi mật khẩu
#define SECRET_CODE "9999"

MFRC522 rfid(SS_PIN, RST_PIN);
Servo myServo;

// Các biến toàn cục
String inputPassword = "";
String correctPassword = "1234";  // Mật khẩu mặc định
int servoAngle = 0;
int wrongAttempts = 0;
const String masterUID = "24F9DABA";
bool isMasterMode = false;
bool isChangingPassword = false;

// Mảng lưu trữ các UID được phép
String authorizedUIDs[MAX_UIDS];
int totalUIDs = 0;

void setup() {
  // Khởi tạo LCD
  lcd.init();
  lcd.backlight();
  lcd.print("Initializing...");

  // Khởi tạo các chân IO
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);

  // Khởi tạo SPI và RFID
  SPI.begin();
  rfid.PCD_Init();

  // Khởi tạo Servo
  myServo.attach(SERVO_PIN);
  myServo.write(0);

  // Đọc dữ liệu từ EEPROM
  loadAuthorizedUIDs();
  loadSavedPassword();

  Serial.begin(9600);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Group 14");
  lcd.setCursor(0, 1);
  lcd.print("Automatic door system");

  delay(2000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Member");
  delay(1500);

  lcd.setCursor(0, 0);
  lcd.print("TRUONG THANH DAT");
  delay(1500);

  lcd.setCursor(0, 0);
  lcd.print("NGUYEN TUAN DUNG");
  delay(1500);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DO DINH ANH DUC");
  delay(1500);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hello!");



}

void loop() {
  if (isMasterMode) {
    handleMasterMode();
  } else {
    handleNormalMode();
  }
}

void handleMasterMode() {
  static bool waitingForCard = false;
  static char lastCommand = '\0';

  if (!waitingForCard) {
    lcd.setCursor(0, 0);
    lcd.print("Master Mode:");
    lcd.setCursor(0, 1);
    lcd.print("A:Add B:Del #:Exit");
  }

  char key = keypad.getKey();
  if (key && !waitingForCard) {
    switch (key) {
      case 'A':
        waitingForCard = true;
        lastCommand = 'A';
        lcd.clear();
        lcd.print("Scan new card");
        break;

      case 'B':
        waitingForCard = true;
        lastCommand = 'B';
        lcd.clear();
        lcd.print("Scan to delete");
        break;

      case '#':
        isMasterMode = false;
        lcd.clear();
        lcd.print("Normal Mode");
        delay(1000);
        break;
    }
  }

  if (waitingForCard) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String cardUID = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
        cardUID += String(rfid.uid.uidByte[i], HEX);
      }
      cardUID.toUpperCase();

      if (lastCommand == 'A') {
        addUID(cardUID);
      } else if (lastCommand == 'B') {
        removeUID(cardUID);
      }

      waitingForCard = false;
      lastCommand = '\0';

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();

      delay(1000);
    }
  }
}

void handleNormalMode() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    openDoor();
    delay(500);
  }

  char key = keypad.getKey();
  if (key) {
    handleKeypad(key);
  }

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String rfidUID = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      rfidUID += String(rfid.uid.uidByte[i], HEX);
    }
    rfidUID.toUpperCase();

    Serial.print("Card detected: ");
    Serial.println(rfidUID);

    if (rfidUID == masterUID) {
      isMasterMode = true;
      tone(BUZZER_PIN, 800, 200);
      lcd.clear();
      lcd.print("Master Mode");
      delay(1000);
    } else {
      bool authorized = false;
      for (int i = 0; i < totalUIDs; i++) {
        if (rfidUID == authorizedUIDs[i]) {
          authorized = true;
          break;
        }
      }

      if (authorized) {
        openDoor();
      } else {
        lcd.clear();
        lcd.print("Access Denied");
        tone(BUZZER_PIN, 1000, 1000);
        delay(2000);
      }
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
}

void handleKeypad(char key) {
  if (isChangingPassword) {
    handlePasswordChange(key);
    return;
  }

  if (wrongAttempts >= 3) {
    lcd.clear();
    lcd.print("System Locked!");
    delay(30000);
    wrongAttempts = 0;
    lcd.clear();
    lcd.print("Enter Password");
    return;
  }

  if (key == '#') {
    if (inputPassword == SECRET_CODE) {
      isChangingPassword = true;
      lcd.clear();
      lcd.print("New Password:");
      inputPassword = "";
      return;
    }

    if (inputPassword == correctPassword) {
      openDoor();
    } else {
      lcd.clear();
      lcd.print("Wrong Password");
      tone(BUZZER_PIN, 1000);
      wrongAttempts++;
      delay(2000);
      noTone(BUZZER_PIN);
    }
    inputPassword = "";
    lcd.clear();
    lcd.print("Enter Password");
  } else if (key == '*') {
    inputPassword = "";
    lcd.clear();
    lcd.print("Cleared");
    delay(1000);
    lcd.clear();
    lcd.print("Enter Password");
  } else {
    inputPassword += key;
    lcd.clear();
    lcd.print("Enter Password:");
    lcd.setCursor(0, 1);
    lcd.print(inputPassword);
  }
}

void handlePasswordChange(char key) {
  if (key == '#') {
    if (inputPassword.length() >= 4) {
      saveNewPassword(inputPassword);
      correctPassword = inputPassword;

      lcd.clear();
      lcd.print("Password Changed!");
      tone(BUZZER_PIN, 800, 200);
      delay(1500);

      isChangingPassword = false;
      inputPassword = "";
      lcd.clear();
      lcd.print("Enter Password");
    } else {
      lcd.clear();
      lcd.print("Too Short!");
      tone(BUZZER_PIN, 400, 500);
      delay(1500);

      lcd.clear();
      lcd.print("New Password:");
      lcd.setCursor(0, 1);
      lcd.print(inputPassword);
    }
  } else if (key == '*') {
    inputPassword = "";
    lcd.clear();
    lcd.print("New Password:");
  } else {
    inputPassword += key;
    lcd.setCursor(0, 1);
    lcd.print(inputPassword);
  }
}

void openDoor() {
  lcd.clear();
  lcd.print("Access Granted");
  lcd.setCursor(0, 1);
  lcd.print("Welcome!");

  tone(BUZZER_PIN, 500);
  digitalWrite(LED_GREEN_PIN, HIGH);
  myServo.write(90);
  delay(1500);
  noTone(BUZZER_PIN);

  unsigned long startTime = millis();
  bool motionDetected = false;

  while (millis() - startTime < 5000) {
    if (digitalRead(PIR_PIN) == HIGH) {
      motionDetected = true;
      digitalWrite(LED_GREEN_PIN, HIGH);
      lcd.clear();
      lcd.print("Motion detected!");
      startTime = millis();
    } else {
      digitalWrite(LED_GREEN_PIN, LOW);  // Tắt LED nếu không có chuyển động
    }
    delay(100);
  }

  myServo.write(0);
  digitalWrite(LED_GREEN_PIN, LOW);
  delay(1000);
}

void loadAuthorizedUIDs() {
  totalUIDs = EEPROM.read(UID_TOTAL_ADDRESS);
  if (totalUIDs > MAX_UIDS) totalUIDs = 0;

  for (int i = 0; i < totalUIDs; i++) {
    String uid = "";
    for (int j = 0; j < UID_LENGTH * 2; j++) {
      char c = EEPROM.read(EEPROM_START_ADDRESS + (i * UID_LENGTH * 2) + j);
      if (c != 0) uid += c;
    }
    authorizedUIDs[i] = uid;
  }
}

void saveAuthorizedUIDs() {
  EEPROM.write(UID_TOTAL_ADDRESS, totalUIDs);
  for (int i = 0; i < totalUIDs; i++) {
    String uid = authorizedUIDs[i];
    for (int j = 0; j < uid.length(); j++) {
      EEPROM.write(EEPROM_START_ADDRESS + (i * UID_LENGTH * 2) + j, uid[j]);
    }
  }
}

void addUID(String uid) {
  for (int i = 0; i < totalUIDs; i++) {
    if (authorizedUIDs[i] == uid) {
      lcd.clear();
      lcd.print("Card exists!");
      tone(BUZZER_PIN, 400, 500);
      delay(1000);
      return;
    }
  }

  if (totalUIDs < MAX_UIDS) {
    authorizedUIDs[totalUIDs++] = uid;
    saveAuthorizedUIDs();
    lcd.clear();
    lcd.print("Card Added!");
    tone(BUZZER_PIN, 800, 200);
    delay(1000);
  } else {
    lcd.clear();
    lcd.print("Memory Full!");
    tone(BUZZER_PIN, 400, 500);
    delay(1000);
  }
}

void removeUID(String uid) {
  bool found = false;
  for (int i = 0; i < totalUIDs; i++) {
    if (authorizedUIDs[i] == uid) {
      for (int j = i; j < totalUIDs - 1; j++) {
        authorizedUIDs[j] = authorizedUIDs[j + 1];
      }
      totalUIDs--;
      saveAuthorizedUIDs();
      found = true;
      break;
    }
  }

  lcd.clear();
  if (found) {
    lcd.print("Card Removed!");
    tone(BUZZER_PIN, 800, 200);
  } else {
    lcd.print("Card Not Found!");
    tone(BUZZER_PIN, 400, 500);
  }
  delay(1000);
}

void saveNewPassword(String newPassword) {
  EEPROM.write(PASSWORD_ADDRESS, newPassword.length());
  for (int i = 0; i < newPassword.length(); i++) {
    EEPROM.write(PASSWORD_ADDRESS + 1 + i, newPassword[i]);
  }
}

void loadSavedPassword() {
  int passLength = EEPROM.read(PASSWORD_ADDRESS);
  if (passLength > 0 && passLength < 20) {
    String savedPassword = "";
    for (int i = 0; i < passLength; i++) {
      savedPassword += char(EEPROM.read(PASSWORD_ADDRESS + 1 + i));
    }
    if (savedPassword.length() > 0) {
      correctPassword = savedPassword;
    }
  }
}
