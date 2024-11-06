#include <SPI.h>                // Serial Peripheral Interface
#include <Wire.h>               // I2C
#include <Adafruit_GFX.h>       // Adafruit graphic library
#include <Adafruit_SSD1306.h>   // oled driver library

// Cablage écran OLED 128x64 pixels I2C basé sur le contrôleur SSD1306
//SCL pin A5
//SDA pin A4
Adafruit_SSD1306 display(128, 64, &Wire);

// cablage Micropstep Driver (pilote de moteur pas à pas)
// les pin ENA-, DIR- et PUL- sont à la masse
// dipswitch = S1-S3 : OFF-ON-OFF (1600 pulse / rev, à valider en fonction des besoins)
// dipswitch S4-S6 : OFF-ON-ON, max 2,5A
#define PULSE_STEPPER 5 // PUL+
#define DIR_STEPPER 6 // DIR+
#define ENABLE_STEPPER 7 // ENA+

// Cablâge encodeur
#define SW_ENCODER  2 // obligatoire sur pin 2 pour assurer la gestion des interrupts sur le nano       
#define CLK_ENCODER 3       
#define DT_ENCODER  4   

// Variables liées à l'encodeur
int etatPrecedentSW;           
int etatPrecedentCLK;          
int etatPrecedentDT; 
int ButtonPressed;    

// Cablagge du microswitch fin de course. Par rapport à la masse, pullup interne actif.
#define LIMIT_SWITCH 8

// Deux modes possibles: 
// graver : dans ce cas, le plateau positionne la focale du laser à la surface de la piece à graver, 
// couper : dans ce cas, le plateau positionne la focale du laser au milieu de la piece à graver
#define GRAVER 1
#define COUPER 0
#define DEFAULT_MODE GRAVER

// Epaisseur maximum de la piece sur le plateau en 1/10e de mm
#define EPAISSEUR_MAX 38
int Epaisseur = EPAISSEUR_MAX; //en 1/10e de mm 
int EpaisseurPrecedente = EPAISSEUR_MAX;

// Steps MIN et MAX du moteur pas a pas dans les limites du mécanisme
#define STEPMIN 0
#define STEPMAX 6000

// Delai d'attente (ms) sans action de l'opérateur avant de commencer la manoeuvre du plateau
#define STARTDELAY 6000 

void setup() {                
  // configure les pin du controleur de moteur pas a pas 
  pinMode(PULSE_STEPPER, OUTPUT);
  pinMode(DIR_STEPPER, OUTPUT);
  pinMode(ENABLE_STEPPER, OUTPUT);

  // configure la pin du fin de course
  pinMode(LIMIT_SWITCH, INPUT_PULLUP);

  // Configure les pin du bouton encodeur KY-040)
  pinMode(SW_ENCODER, INPUT);
  pinMode(CLK_ENCODER, INPUT);
  pinMode(DT_ENCODER, INPUT);

  // Petite pause pour laisser le temps aux signaux de se stabiliser
  delay(200);

  // disable le moteur pas a pas -> pas de surchauffe, moins de conso
  digitalWrite(ENABLE_STEPPER, HIGH); 
  
  // Mémorisation des valeurs initiales SW/CLK/DT de l'encodeur 
  etatPrecedentSW  = digitalRead(SW_ENCODER);
  etatPrecedentCLK = digitalRead(CLK_ENCODER);
  etatPrecedentDT  = digitalRead(DT_ENCODER);

  // initialise l'ecran OLED
  //Utiliser le scanner I2C pour trouver le port série sur lequel se trouve votre écran 
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  

  // Activation des interruptions sur les lignes CLK et DT pour la lecture de l'encodeur
  attachInterrupt(digitalPinToInterrupt(CLK_ENCODER), changementCLK, CHANGE);   
  attachInterrupt(digitalPinToInterrupt(SW_ENCODER), changementSW, FALLING);   

  Serial.begin(9600);
}

void loop() {
  unsigned long start;
  int Mode=DEFAULT_MODE; 

  // ne fait rien, attends que l'operateur presse le bouton pour commencer les opérations
  RefreshStatusScreen( Epaisseur/10.0, Mode);
  do { delay( 200);  } while (!ButtonPressed);
  ButtonPressed = LOW;

  // L'operatreur peut selectionner l'épaisseur de la piece en tournant le bouton
  // et le mode graver/couper en poussant sur le bouton
  // ensuite, s'il ne touche plus a rien pendant "STARTDELAY" milli-seconde, la manoeuvre commence
  start = millis();
  do
  {
    // Limite les épaisseurs min et max
    Epaisseur = constrain( Epaisseur, 0, EPAISSEUR_MAX);

    // Change le mode sur pression du bouton
    if (ButtonPressed)
    {
      Mode = ! Mode;
      ButtonPressed = LOW;
      start = millis(); // reset timeout
    }
    if (EpaisseurPrecedente != Epaisseur) start = millis(); // reset timeout
    EpaisseurPrecedente = Epaisseur;
    RefreshSetupScreen( Epaisseur/10.0 , Mode, map( millis()-start, 0, STARTDELAY, 63, 0));
  } while ((millis() - start) < STARTDELAY);

  // positionne le plateau réglable
  SetBed( Epaisseur, Mode);  
}


// Epaisseur en 10e de mm
// Mode Graver ou Couper
void SetBed( int Epaisseur, int Mode)
{
  int limit_touched; //contact de fin de course
  int Steps;
  
  // Calcule le nombre de pas nécessaires pour placer la focale sur la surface du matérieux a traiter (graver) ou au milieu de celui-ci (couper)
  if (Mode == COUPER) Epaisseur = Epaisseur / 2;
  Steps = map(Epaisseur, 0, EPAISSEUR_MAX , STEPMAX, STEPMIN);
  
  // Descend chercher le point de référence pour calibration
  digitalWrite(ENABLE_STEPPER, LOW); // enable moteur pas a pas
  DisplayText( 2, "Calibrate");
  digitalWrite(DIR_STEPPER, LOW);    
  do
  {
    digitalWrite(PULSE_STEPPER, HIGH); 
    delayMicroseconds(200);     
    digitalWrite(PULSE_STEPPER, LOW);  
    delayMicroseconds(200);  
    limit_touched = digitalRead( LIMIT_SWITCH);   
  } while (!limit_touched);
  DisplayText( 2, "Done");
  delay( 1000);

  // remonte le bed a la hauteur désirée
  DisplayText( 2, "Set Bed");
  digitalWrite(DIR_STEPPER, HIGH);
  for (long i=0; i< Steps; i++)
  {
    digitalWrite(PULSE_STEPPER, HIGH); 
    delayMicroseconds(200);     
    digitalWrite(PULSE_STEPPER, LOW);  
    delayMicroseconds(200); 
  }  
  digitalWrite(ENABLE_STEPPER, HIGH); // disable moteurc pas a pas

  DisplayText( 2, "Ready");
  delay( 1000);
}

void RefreshSetupScreen( float ep, int mode, int GraphBar)
{
  display.clearDisplay();
  display.setTextColor(WHITE, BLACK);
  display.setTextSize(2);
  display.setCursor(0,0);
  display.print( "Setup");
  display.drawLine(0, 16, 120, 16, WHITE);
  
  display.setCursor(0,20);
  display.print( "w: ");
  display.print(ep);
  display.print( " mm");
  
  display.setCursor(0,38);
  display.print( "m: ");
  if (mode==1) display.println("ENGRAVE"); 
          else display.println("CUT"); 

  // dessine la loading bar à droite de l'ecran, permet d'indiquer quandle délai d'attent est dépassé -> début de la manoeuvre
  for (int i=0; i<GraphBar; i++)
  {
   display.drawLine(123, 60-i, 127, 60-i, WHITE);
  }

  display.setTextColor(BLACK, WHITE);
  display.setTextSize(1);
  display.setCursor(0,57);
  display.println("Press button->mode");
  display.display();
}

void RefreshStatusScreen( float ep, int mode)
{
  display.clearDisplay();
  display.setTextColor(WHITE, BLACK);
  display.setTextSize(2);
  display.setCursor(0,0);
  display.print( "Bed Height");
  display.drawLine(0, 16, 120, 16, WHITE);
  
  display.setCursor(0,20);
  display.print( "w: ");
  display.print(ep);
  display.print( " mm");
  
  display.setCursor(0,38);
  display.print( "m: ");
  if (mode==1) display.println("ENGRAVE"); 
          else display.println("CUT"); 

  display.setTextColor(BLACK, WHITE);
  display.setTextSize(1);
  display.setCursor(0,57);
  display.println("Press button->setup");
  
  display.display();
}

void DisplayText( int size, String t)
{
 // text display tests
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(size);
  display.setCursor(0,0);
  display.println(t);
  display.display();
}

// Lit la position du bouton encodeur
void changementCLK() {
    int etatActuelCLK = digitalRead(CLK_ENCODER);
    int etatActuelDT  = digitalRead(DT_ENCODER);

    // Incrémente: Si CLK = 1 et DT = 0, et que l'ancienCLK = 0 et ancienDT = 1, alors le bouton a été tourné d'un cran vers la droite (sens horaire)
    if((etatActuelCLK == HIGH) && (etatActuelDT == LOW) && (etatPrecedentCLK == LOW) && (etatPrecedentDT == HIGH)) { Epaisseur++; }

    //Décrémente : Si CLK = 1 et DT = 1, et que l'ancienCLK = 0 et ancienDT = 0, alors le bouton a été tourné d'un cran vers la gauche (sens antihoraire)
    if((etatActuelCLK == HIGH) && (etatActuelDT == HIGH) && (etatPrecedentCLK == LOW) && (etatPrecedentDT == LOW)) { Epaisseur--; }

    // Et on mémorise ces états actuels comme étant "les nouveaux anciens", pour le "tour suivant" !
    etatPrecedentCLK = etatActuelCLK;
    etatPrecedentDT = etatActuelDT;
}

// Lit l'etat du bouton a presser de l'encodeur + debouncing
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 300;
void changementSW() {
    // on prene en compte le bouton enfoné (falling) si le debounceDelay est écoulé
    if ((millis()-lastDebounceTime) > debounceDelay) 
    {
      lastDebounceTime = millis();
      ButtonPressed = HIGH;
    }
}
