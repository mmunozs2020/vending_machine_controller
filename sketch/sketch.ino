#include <Thread.h>
#include <StaticThreadController.h>
#include <ThreadController.h>
#include <LiquidCrystal.h>
#include <DHT11.h>
#include <avr/wdt.h>



// GLOBAL CONSTANTS -----------------------------------------------
const int BUTTON_PIN  = 0;                // button

const int RED_LED     = 8;                // leds
const int GREEN_LED   = 9;

const int TRIGGER_PIN = 13;               // ultrasonic sensor
const int ECHO_PIN    = 10;
const long BOUNCE_1   = 6000;

const int DHT_PIN     = 12;               // DHT11 sensor

const int ADMIN_MILLIS      = 5000;
const int RE_SERVICE_LM     = 2000; // REstart SERVICE Lower Margin
const int RE_SERVICE_UM     = 3000; // ''''''''''''''' Upper Margin

const int FAST_INTERVAL     = 200;
const int DEFAULT_INTERVAL  = 300;
const int SLOW_INTERVAL     = 400;
const int STARTUP_MILLIS    = 6000;
const int STARTUP_INTERVAL  = 1000;
const int TH_MILLIS         = 5000;
const int RETIRE_MILLIS     = 3000;

const int JOYSTICK_UP       = 915;        // joystick
const int JOYSTICK_DOWN     = 80;         //
const int JOYSTICK_SWITCH   = 11; // switch pin
const int JOYSTICK_LEFT     = 85;

const int NUM_OF_PRODUCTS   = 5;


// GLOBAL VARIABLES -----------------------------------------------
bool b_was_pressed           = false;  // button related
unsigned long b_time_pressed = 0;

bool wait_text_written = false;  // lcd related (waiting thread)
bool product_written   = false;  // lcd related (th and service)
bool new_status        = true;   // lcd related (service start)
bool new_index         = false;  // lcd related (messages printed -> service)
bool new_service       = true;   // lcd related (service <--> options)

int service_start      = 0;
int status             = 0;   // =1 means service / =2 means admin
                              // this variable sets the list the joystick works on
int index                 = 0;
unsigned long prep_time   = 0;   // this will store the random time of the coffee preparation
unsigned long coffee_time = 0;   // = millis() + prep_time
bool coffee_text_written  = false;

String products[NUM_OF_PRODUCTS] = {
  "Cafe Solo",
  "Cafe Cortado",
  "Cafe Doble",
  "Cafe Premium",
  "Chocolate"
};

float prizes[NUM_OF_PRODUCTS] = {
  1.00,
  1.10,
  1.25,
  1.50,
  2.00
};

String admin_menu[4] = {
  "Ver temperatura",
  "Ver dist sensor",
  "Ver contador",
  "Cambiar precios"
};
int admin_index           = 0;



LiquidCrystal lcd(7, 6, 4, 5, 3, 2);
DHT11 dht11(DHT_PIN);



// THREADS -------------------------------------------------------------------------------
Thread startup_thread = Thread();
Thread waiting_thread = Thread();
Thread temphum_thread = Thread();
Thread service_thread = Thread();
Thread options_thread = Thread();
Thread button__thread = Thread(); //***
Thread cooking_thread = Thread();

Thread admin_menu_thread = Thread();
Thread any_option_thread = Thread();    //===> any_option_callback()
Thread menu_control_thread = Thread();  // joystick movement control (U,D,L)


// FUNCTIONS -----------------------------------------------------------------------------
void read_and_print_th() {
  lcd.setCursor(0,0);
  lcd.clear();

  float h = dht11.readHumidity();
  String strh = String(h, 2);
  float t = dht11.readTemperature();
  String strt = String(t, 2);

  lcd.print("Temp: " + strt + " C");
  lcd.setCursor(0,2);
  lcd.print("Hum: " + strh + " %");
  lcd.setCursor(0,0);
}


bool read_distance(bool print_data) {
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);

  long bounce_time = pulseIn(ECHO_PIN, HIGH);

  if(print_data) {
    lcd.setCursor(0,0);
    lcd.clear();

    float dist = (float)(0.01715 * bounce_time);
    String strdist = String(dist, 2);
    
    lcd.print("Dist: " + strdist + " cm");
  }
  
  if (bounce_time < BOUNCE_1) {
    return true;
  }
  return false;
}


void print_seconds() {
  lcd.setCursor(0,0);
  lcd.clear();

  float secs = (float) (millis() / 1000);
  String strsecs = String(secs, 0);
  lcd.print(strsecs);
}


bool check_service_loop() {
  if(status == 2) {
    return false;
  }
  if(temphum_thread.enabled) {
    return true;
  }
  if(service_thread.enabled) {
    return true;
  }
  if(cooking_thread.enabled) {
    return true;
  }
  return false;
}


void restart_service_globals(const int next_status) {
  // Restarts all the global variables affecting the service functionality loop
  wait_text_written     = false;
  product_written       = false;
  new_status            = true;
  new_index             = false;
  new_service           = true;
  
  service_start         = 0;
  // Set next_status to 1 when restarting service loop, to 2 when entering admin menu
  status                = next_status;   // =1 means service / =2 means admin
  
  index                 = 0;
  prep_time             = 0;
  coffee_time           = 0;
  coffee_text_written   = false;
}


void admin_leds() {
  if(menu_control_thread.enabled) {
    digitalWrite(RED_LED, HIGH);
    analogWrite(GREEN_LED, 255);
  } else {
    digitalWrite(RED_LED, LOW);
    analogWrite(GREEN_LED, 0);
  }
}


void switch_admin_menu() {
  if(status == 2) {
    restart_service_globals(1);
    waiting_thread.enabled = true;
    temphum_thread.enabled = false;
    service_thread.enabled = false;
    options_thread.enabled = false;
    cooking_thread.enabled = false;
    admin_menu_thread.enabled = false;
    any_option_thread.enabled = false;
    menu_control_thread.enabled = false;
    admin_leds();
  } else {
    restart_service_globals(2);
    waiting_thread.enabled = false;
    temphum_thread.enabled = false;
    service_thread.enabled = false;
    options_thread.enabled = false;
    cooking_thread.enabled = false;
    admin_menu_thread.enabled = true;
    any_option_thread.enabled = false;
    menu_control_thread.enabled = true;
    admin_leds();
  }
}



// CALLBACKS -----------------------------------------------------------------------------
void callback_startup() { // -------------- startup callback
  static bool ledStat = false;
  ledStat = !ledStat;

  digitalWrite(RED_LED, ledStat);

  if(millis() == STARTUP_MILLIS) {
    startup_thread.enabled = false;
    waiting_thread.enabled = true;
    lcd.clear();
  }
}


void callback_waiting() { // -------------- waiting callback
  status = 1; 
  if (!wait_text_written) {
    lcd.setCursor(0,0);
    lcd.clear();
    
    wait_text_written = true;
    lcd.print("ESPERANDO");
    lcd.setCursor(0,2);
    lcd.print("CLIENTE");
  }
  
  if (read_distance(false)) {
    waiting_thread.enabled = false;
    temphum_thread.enabled = true;
  }
}


void callback_temphum() { // -------------- temphum callback
  if(new_service) {
    new_service = false;
    service_start = millis();
    lcd.setCursor(0,0);
    lcd.clear();
  }
  
  int service_elapsed = millis() - service_start;
  if(service_elapsed <= TH_MILLIS){
    if(!product_written){
      read_and_print_th();
      product_written = true;
      new_status = true;
    }
  } else {
    temphum_thread.enabled = false;
    service_thread.enabled = true;
    options_thread.enabled = true;
  }
}


void callback_service() { // -------------- service callback
  
  if(new_status){
    lcd.setCursor(0,0);
    lcd.clear();
    new_status = !new_status;
    index = 0;
    product_written = false;
  }
  if(new_index){
    lcd.setCursor(0,0);
    lcd.clear();
    new_index = false;
    product_written = false;
  }
  if(!product_written) {
    lcd.print(products[index]);
    lcd.setCursor(0,2);
    lcd.print(prizes[index]);
    lcd.print(" e");
    lcd.setCursor(0,0);
    product_written = true;
  }
  // Here we control the selection of the product the user can do
  if(product_written) {
    int switch_state = digitalRead(JOYSTICK_SWITCH);
    if(switch_state == LOW) {
      new_status = true;
      cooking_thread.enabled = true;
      service_thread.enabled = false;
      options_thread.enabled = false;
    }
  }
}


void callback_options() { // -------------- options callback (joystick movement)
  int joystick_x = analogRead(A0);
  
  if(joystick_x > JOYSTICK_UP){
    index = index -1;
    if(index < 0){
      index = NUM_OF_PRODUCTS-1;
    }
    new_index = true;
  }

  if(joystick_x < JOYSTICK_DOWN){
    index = index +1;
    if(index == NUM_OF_PRODUCTS){
      index = 0;
    }
    new_index = true;
  }
}


void callback__button() { // -------------- button callback ***
  
  if(b_was_pressed) { //------------(1) check if we already pressed the button
    int bcheck = digitalRead(BUTTON_PIN);
    if(bcheck == HIGH) {
      Serial.println("pulsao");
      
      int timelapse = millis() - b_time_pressed;

      // ---------------------------(2) check if we enter admin options
      if(timelapse > ADMIN_MILLIS) {
        switch_admin_menu();
      } else {
        // -------------------------(3) check if we can restart the service
        if(check_service_loop()) {
          if(timelapse > RE_SERVICE_LM) {
            if(timelapse < RE_SERVICE_UM) {
              restart_service_globals(1);
              waiting_thread.enabled = true;
              temphum_thread.enabled = false;
              service_thread.enabled = false;
              options_thread.enabled = false;
              button__thread.enabled = true;
              cooking_thread.enabled = false;
            }
          }
        }
      }
      b_was_pressed = false;
      b_time_pressed = 0;
    }

  } else {  // ---------------------(*) in case the button is not pressed yet
    int bstate = digitalRead(BUTTON_PIN);
    if(bstate == LOW) {
      b_was_pressed = true;
      b_time_pressed = millis();
    }
  }
}


void callback_cooking() { // -------------- cooking callback (preparing coffee)

  if(prep_time == 0) {
    unsigned long current_time = millis();
    prep_time = random(4000, 8000);
    coffee_time = current_time + prep_time;

    lcd.setCursor(0,0);
    lcd.clear();
    lcd.print("Preparando");
    lcd.setCursor(0,2);
    lcd.print("cafe ...");
    
  } else {
    if(coffee_time < millis()) {
      if(!coffee_text_written) {
        coffee_text_written = true;
        lcd.setCursor(0,0);
        lcd.clear();
        lcd.print("RETIRE BEBIDA");
      }
      unsigned long difference = millis() - RETIRE_MILLIS;
      if(coffee_time < difference) {
        analogWrite(GREEN_LED, 0);
        coffee_text_written    = false;
        wait_text_written      = false;
        product_written        = false;
        new_status             = true;
        new_service            = true;
        cooking_thread.enabled = false;
        waiting_thread.enabled = true;
        prep_time = 0;
      }
    } else {
      int elapsed = millis() - (coffee_time - prep_time);
      int brightness = map(elapsed, 0, prep_time, 0, 255);
      analogWrite(GREEN_LED, brightness);
    }
  }
}



// ADMIN MENU CALLBACKS -----------------------
void callback_admin_menu() { // -------------- admin menu callback (lcd)
  
  if(new_status){
    lcd.setCursor(0,0);
    lcd.clear();
    new_status = !new_status;
    admin_index = 0;
    product_written = false;
  }
  if(new_index){
    lcd.setCursor(0,0);
    lcd.clear();
    new_index = false;
    product_written = false;
  }
  if(!product_written) {
    lcd.print(admin_menu[admin_index]);
    lcd.setCursor(0,2);
    lcd.print(" [SELECT]");
    lcd.setCursor(0,0);
    product_written = true;
  }
  // Here we control the selection of the product the user can do
  if(product_written) {
    int switch_state = digitalRead(JOYSTICK_SWITCH);
    if(switch_state == LOW) {
      new_status = true;
      any_option_thread.enabled = true;
      admin_menu_thread.enabled = false;
    }
  }
}


void menu_control_callback() { // -------------- admin menu options (joystick movement)
  int joystick_x = analogRead(A0);
  int joystick_y = analogRead(A1);
  
  if(joystick_x > JOYSTICK_UP){
    admin_index = admin_index -1;
    if(admin_index < 0){
      admin_index = 3;
    }
    new_index = true;
  }

  if(joystick_x < JOYSTICK_DOWN){
    admin_index = admin_index +1;
    if(admin_index == 4){
      admin_index = 0;
    }
    new_index = true;
  }

  if(joystick_y < JOYSTICK_LEFT){
    if(any_option_thread.enabled) {
      any_option_thread.enabled = false;
      admin_menu_thread.enabled = true;
    }
  }
}


void any_option_callback() {
  // This function operates in case any option from the admin menu has been selected
  switch(admin_index) {
    case 0:
      read_and_print_th();
      break;

    case 1:
      read_distance(true);
      break;

    case 2:
      print_seconds();
      break;

    case 3:
      Serial.println("admin prizes");
      break;
  }
}



// SETUP -------------------------------------------------------------------------------
void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  randomSeed(analogRead(A0));

  // Watchdog
  wdt_disable();
  wdt_enable(WDTO_8S);
  

  lcd.begin(16,2);
  lcd.setCursor(0,1);
  lcd.clear();
  lcd.print("CARGANDO...");

  pinMode(BUTTON_PIN, INPUT);
  pinMode(JOYSTICK_SWITCH, INPUT_PULLUP);

  pinMode(RED_LED, OUTPUT);
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  digitalWrite(TRIGGER_PIN, LOW);
  digitalWrite(ECHO_PIN, LOW);
  delayMicroseconds(2);

  // STARTUP THREAD
  startup_thread.enabled = true;
  startup_thread.setInterval(STARTUP_INTERVAL);
  startup_thread.onRun(callback_startup);

  // WAITING THREAD
  waiting_thread.enabled = false;
  waiting_thread.setInterval(DEFAULT_INTERVAL);
  waiting_thread.onRun(callback_waiting);

  // TEMPERATURE AND HUMIDITY THREAD
  temphum_thread.enabled = false;
  temphum_thread.setInterval(DEFAULT_INTERVAL);
  temphum_thread.onRun(callback_temphum);

  // SERVICE THREAD
  service_thread.enabled = false;
  service_thread.setInterval(FAST_INTERVAL);
  service_thread.onRun(callback_service);

  // OPTIONS THREAD
  options_thread.enabled = false;
  options_thread.setInterval(DEFAULT_INTERVAL);
  options_thread.onRun(callback_options);

  // BUTTON THREAD ***
  button__thread.enabled = true;
  button__thread.setInterval(FAST_INTERVAL);
  button__thread.onRun(callback__button);

  // PREPARING THE COFFEE THREAD (COOKING)
  cooking_thread.enabled = false;
  cooking_thread.setInterval(DEFAULT_INTERVAL);
  cooking_thread.onRun(callback_cooking);


  // ADMIN MENU THREAD
  admin_menu_thread.enabled = false;
  admin_menu_thread.setInterval(FAST_INTERVAL);
  admin_menu_thread.onRun(callback_admin_menu);

  // ADMIN OPTION SELECTED THREAD
  any_option_thread.enabled = false;
  any_option_thread.setInterval(FAST_INTERVAL);
  any_option_thread.onRun(any_option_callback);

  // ADMIN MENU CONTROL THREAD (JOYSTICK) 
  menu_control_thread.enabled = false;
  menu_control_thread.setInterval(DEFAULT_INTERVAL);
  menu_control_thread.onRun(menu_control_callback);
  
}

// LOOP  -------------------------------------------------------------------------------
void loop() {
  // MAIN CODE 
  if(startup_thread.shouldRun()){
    startup_thread.run();
  }
  if(waiting_thread.shouldRun()){
    waiting_thread.run();
  }
  if(temphum_thread.shouldRun()){
    temphum_thread.run();
  }
  if(service_thread.shouldRun()){
    service_thread.run();
  }
  if(options_thread.shouldRun()){
    options_thread.run();
  }
  if(button__thread.shouldRun()){
    button__thread.run();
  }
  if(cooking_thread.shouldRun()){
    cooking_thread.run();
  }
  
  if(admin_menu_thread.shouldRun()){
    admin_menu_thread.run();
  }
  if(any_option_thread.shouldRun()){
    any_option_thread.run();
  }
  if(menu_control_thread.shouldRun()){
    menu_control_thread.run();
  }

  wdt_reset();
}