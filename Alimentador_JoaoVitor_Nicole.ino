#include <Arduino.h>
#include <FB_Const.h>
#include <FB_Error.h>
#include <FB_Network.h>
#include <FB_Utils.h>
#include <Firebase.h>
#include <FirebaseFS.h>
#include <Firebase_ESP_Client.h>
#include <MB_NTP.h>
#include "esp_camera.h"
#include "SPI.h"
#include "driver/rtc_io.h"
#include "ESP32_MailClient.h"
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <Stepper.h> //INCLUSÃO DE BIBLIOTECA
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <esp_task_wdt.h>

// CONFIG MOTOR
const int stepsPerRevolution = 65;
Stepper myStepper(stepsPerRevolution, 12,15,13,14);

// VALORES GLOBAIS
const char* ssid = "SOL";
const char* password = "cms201244";
#define API_KEY "AIzaSyBqTqw0zQBFj5UFpO2ruNwAM8CcMRRyzhk"
#define DATABASE_URL "https://alimentador-21872-default-rtdb.firebaseio.com/"
#define pin_FLASH 4
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config_firebase;
bool signupOK = false;
unsigned long sendDataPrevMillis = 0;
String intValue;
bool dar_Comida = false;
int quantidade_comida = 0;
TaskHandle_t TaskFirebaseHandler;
TaskHandle_t TaskAlimentarHandler;

// Config do sistema SMTP
#define emailSenderAccount    "botfotogato@gmail.com"
#define emailSenderPassword   "kjsiuopfjuqbkgkk"
#define smtpServer            "smtp.gmail.com"
#define smtpServerPort        465
#define emailSubject          "Foto"
#define emailRecipient        "vitorkogut@gmail.com"

// CONFIG CAMERA
#define CAMERA_MODEL_AI_THINKER
#if defined(CAMERA_MODEL_AI_THINKER)
  #define PWDN_GPIO_NUM     32
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM      0
  #define SIOD_GPIO_NUM     26
  #define SIOC_GPIO_NUM     27
  #define Y9_GPIO_NUM       35
  #define Y8_GPIO_NUM       34
  #define Y7_GPIO_NUM       39
  #define Y6_GPIO_NUM       36
  #define Y5_GPIO_NUM       21
  #define Y4_GPIO_NUM       19
  #define Y3_GPIO_NUM       18
  #define Y2_GPIO_NUM        5
  #define VSYNC_GPIO_NUM    25
  #define HREF_GPIO_NUM     23
  #define PCLK_GPIO_NUM     22
#else
  #error "Camera model not selected"
#endif

// The Email Sending data object contains config and data to send
SMTPData smtpData;
// Photo File Name to save in SPIFFS
#define FILE_PHOTO "/photo.jpg"

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  
  Serial.begin(115200);
  Serial.println();
  pinMode(pin_FLASH,OUTPUT);
  
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  // MONTAGEM DO SPIFF
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    ESP.restart();
  }
  else {
    delay(500);
    Serial.println("SPIFFS mounted successfully");
  }
  
  // Print ESP32 Local IP Address
  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());

  //INIT DA CAMERA
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // INIT FIREBASE
  config_firebase.api_key = API_KEY;
  config_firebase.database_url = DATABASE_URL;
  if (Firebase.signUp(&config_firebase, &auth, "", "")){
    Serial.println("Conectado ao Firebase\n");
    signupOK = true;
  }
  else{
    Serial.printf("Deu ruim %s\n", config_firebase.signer.signupError.message.c_str());
  }
  config_firebase.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config_firebase, &auth);
  Firebase.reconnectWiFi(true);

  //Incialização das Tasks
  xTaskCreatePinnedToCore(
             TaskDarComida, /* Task function. */
             "Task1",   /* name of task. */
             10000,     /* Stack size of task */
             NULL,      /* parameter of the task */
             1,         /* priority of the task */
             &TaskAlimentarHandler,    /* Task handle to keep track of created task */
             1);        /* pin task to core 0 */
  
}

// Check if photo capture was successful
bool checkPhoto( fs::FS &fs ) {
  File f_pic = fs.open( FILE_PHOTO );
  unsigned int pic_sz = f_pic.size();
  return ( pic_sz > 100 );
}

// Capture Photo and Save it to SPIFFS
void capturePhotoSaveSpiffs( void ) {
  camera_fb_t * fb = NULL; // pointer
  bool ok = 0; // Boolean indicating if the picture has been taken correctly

  do {
    // Take a photo with the camera
    Serial.println("Taking a photo...");
    digitalWrite(pin_FLASH,HIGH);
    delay(10);
    fb = esp_camera_fb_get();
    digitalWrite(pin_FLASH,LOW);
    if (!fb) {
      Serial.println("Camera capture failed");
      return;
    }
    // Photo file name
    Serial.printf("Picture file name: %s\n", FILE_PHOTO);
    File file = SPIFFS.open(FILE_PHOTO, FILE_WRITE);
    // Insert the data in the photo file
    if (!file) {
      Serial.println("Failed to open file in writing mode");
    }
    else {
      file.write(fb->buf, fb->len); // payload (image), payload length
      Serial.print("The picture has been saved in ");
      Serial.print(FILE_PHOTO);
      Serial.print(" - Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    // Close the file
    file.close();
    esp_camera_fb_return(fb);
    // check if file has been correctly saved in SPIFFS
    ok = checkPhoto(SPIFFS);
  } while ( !ok );
}

void sendPhoto( void ) {
  // Preparing email
  Serial.println("Sending email...");
  // Set the SMTP Server Email host, port, account and password
  smtpData.setLogin(smtpServer, smtpServerPort, emailSenderAccount, emailSenderPassword);
  // Set the sender name and Email
  smtpData.setSender("PetFeed", emailSenderAccount);
  // Set Email priority or importance High, Normal, Low or 1 to 5 (1 is highest)
  smtpData.setPriority("High");
  // Set the subject
  smtpData.setSubject(emailSubject);
  // Set the email message in HTML format
  smtpData.setMessage("<h2>Foto de seu gatito tirada!.</h2>", true);
  // Set the email message in text format
  //smtpData.setMessage("Photo captured with ESP32-CAM and attached in this email.", false);
  //Add recipients, can add more than one recipient
  smtpData.addRecipient(emailRecipient);
  //smtpData.addRecipient(emailRecipient2);
  // Add attach files from SPIFFS
  smtpData.addAttachFile(FILE_PHOTO, "image/jpg");
  // Set the storage type to attach files in your email (SPIFFS)
  smtpData.setFileStorageType(MailClientStorageType::SPIFFS);
  smtpData.setSendCallback(sendCallback);
  // Start sending Email, can be set callback function to track the status
  if (!MailClient.sendMail(smtpData))
    Serial.println("Error sending Email, " + MailClient.smtpErrorReason());
  // Clear all data from Email object to free memory
  smtpData.empty();
}

// Callback function to get the Email sending status
void sendCallback(SendStatus msg) {
  //Print the current status
  Serial.println(msg.info());
}

// Task alimentar
void TaskDarComida(void * pvParameters){
  while(1==1){
    if( dar_Comida == true){
      dar_Comida = false;
      Serial.printf("\n Dando %d porcoes", quantidade_comida);
      for(int i=0; i<quantidade_comida; i++){
        myStepper.setSpeed(300);
        myStepper.step(4500);
        Serial.print("rodando");
        myStepper.step(-4500);
      }
      delay(20000);
      capturePhotoSaveSpiffs();
      sendPhoto();
    }
    delay(100);
  }
}


void loop(){
    if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 10000 || sendDataPrevMillis == 0)) {
      Serial.write("\n\nChecando comandos pendentes: ");
      sendDataPrevMillis = millis();
      if (Firebase.RTDB.getInt(&fbdo, "/comandos/comando/executado")) {
        int newCommand = fbdo.intData();
        if (newCommand == 0){
          esp_task_wdt_reset();
          Serial.write("Comando pendente encontrado!");
          if (Firebase.RTDB.getString(&fbdo, "/comandos/comando/quantidade")) {
            int newquantidade = fbdo.stringData().toInt();
            Firebase.RTDB.setInt(&fbdo, "/comandos/comando/executado", 1);
            quantidade_comida = newquantidade;
            dar_Comida = true;
          }else{
            Serial.println(fbdo.errorReason());
          }
        }else{
          Serial.write("nenhum comando pendente");
        }
      }
      else {
        Serial.println(fbdo.errorReason());
      }
    }
}
