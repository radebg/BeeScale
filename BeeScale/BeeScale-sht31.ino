/*
 Name:		BeeScale.ino
 Created:	10/18/2018 9:36:26 PM
 Author:	radebg
 Microcontroller: ArduinoProMini 8Mhz, 3.3V
*/

//#include <Adafruit_Sensor.h>
#include "Adafruit_SHT31.h"
#include <SoftwareSerial.h>
#include <Wire.h>
#include <DS3231.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <HX711.h>
#include "SparkFunBME280.h"

//Initialisation of HX711 ( -Load cell amplifier- )
//----------------------------------------------
#define DOUT A2				//arduino pin for DOUT signal
#define CLK  A1				//arduino pin for CLK signal
HX711 scale(DOUT, CLK);		//initialisation of HX711

// TODO: calibrate scale setup based on calibration sketch
// last good calibration data was -39750 but it must!!! be checked
// explanation below

//Change this calibration factor as per your load cell once it is found you many need to vary it in thousands
float scaleCalibrationFactor = -20350;	//Calibration factor for the scale 

float weight;							//Global variable for storing current weight on the scales
int weightInGrams;

bool reseted = false;					//variable for falg that scale is resseted for today
//----------------------------------------------
// End of initialisation of HX711 (Load cell amplifier)



// initialisation of BMP280 sensor for Temperature, pressure and altitude
//----------------------------------------------
BME280 bme;	// Define Pressure sensor class
float bmeTemperature;	// bme sensor variable for temperature
float bmePressure;		// bme sensor variable for pressure
float bmeAltitude;		// bme sensor variable for Altitude
float bmeHumid;
//----------------------------------------------
// end of initialisation of BMP280 sensor for temperature, pressure and altitude

// initialisation of SHT31 sensor for Temperature and Humid
//----------------------------------------------
Adafruit_SHT31 sht31 = Adafruit_SHT31();
//----------------------------------------------
// initialisation of SHT31 sensor for Temperature and Humid
dht11 dht;
#define DHTPIN 5			//pin for DHT sensor
DS3231 clock;
RTCDateTime dt;
const byte wakePin = 2;					// pin used for waking up the Arduino (interrupt 0)
const byte wakePin2 = 3;				// pin used for waking up the Arduino (interrupt 1)
const byte gsmWakePin = 4;				// pin used for waking up the GSM module from sleep mode

char print_date[16];

//float battValue;
float voltage;
int batteryMax = 100;
//bool lowBattSend = false;

// soil measurement
float soilMoisture;
float soilReading;

//String atResponse;

//String apn = "internet";				// APN of your provider
//String username = "";					// username for your gprs provider
//String password = "";					// password for your grps provider
const String thingSpeakUpadate = "GET http://api.thingspeak.com/update?api_key=03SUMGLJ4MO9KAZK&";

// initializing working variables
float currentTemperature;			// current measured temperature
float currentHumidity;				// current measured relative air humidity

SoftwareSerial gsmSerial(8, 7);		// Define pins for communicating with gsm module

DS3231 Clock;								//define class for DS3231 clock
byte ADay, AHour, AMinute, ASecond, ABits;	// define clock variables
bool ADy, A12h, Apm;						//define clock variables
int correction;

void setup()
{
	pinMode(6, OUTPUT);
	//Scale setup
	scale.set_scale(scaleCalibrationFactor);	//Calibration Factor obtained from calibrating sketch
	scale.tare();								//Reset the scale to 0  

	digitalWrite(gsmWakePin, HIGH);
	analogReference(INTERNAL);

	//initialise Arduino pin for GSM sleep mode (LOW state equals no sleep)
	pinMode(gsmWakePin, OUTPUT);
	digitalWrite(gsmWakePin, LOW);

	// initialize serial port
	Serial.begin(9600);
	Serial.println("...+++++++++...1"); //debugging purposes only, can comment whole line

	// initialize rtc communication trough i2c port (on A4 and A5)
	Wire.begin();
	Serial.println("...+++++++++...2"); //debugging purposes only, can comment whole line

	// initialize software serial port for communication with gsm module SIM800l
	gsmSerial.begin(9600);
	Serial.println("...+++++++++...3"); //debugging purposes only, can comment whole line
	// Initialize DS3231
	clock.begin();

	/*
	Read system date and time and put it on ds3231 chip
	Uncomment next line only first time when U need to set up ds3231 clock (first time U upload skech to arduino).
	Right after that U need to upload sketc with commented same next line 
	It must be done because every time U reset arduino it will upload system date and time. And if arduino is not resetted via PC
	(pressing the button, power on and off...) it will upload wrong date and time.

	If you already setup clock on ds3231 keep next line commented
	*/
	//clock.setDateTime(__DATE__, __TIME__);
	Serial.println("...+++++++++...4"); //debugging purposes only, can comment whole line

	// Disable square wave output (use alarm triggering)
	// setting 0Eh register on ds3231. very important! see technical specs of ds3231!!
	Wire.beginTransmission(0x68);
	Wire.write(0x0e);
	Wire.write(0b00110111);
	Wire.endTransmission();
	Serial.println("...+++++++++...5"); //debugging purposes only, can comment whole line

	// Disable DS3231 Alarms 1 and 2 at the begining
	clock.armAlarm1(false);
	clock.armAlarm2(false);
	clock.clearAlarm1();
	clock.clearAlarm2();
	Serial.println("...+++++++++...6");

	// Put PIN 2 as an Interrupt (0)
	// Put PIN 3 as an Interrupt (1)
	// For now only interrupt 0 is used (for waking up after alarm trigger)
	// Second interrupt could be used for emergency trigger (someone move beehive, huge loss in weight...)
	pinMode(wakePin, INPUT_PULLUP);
	pinMode(wakePin2, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(wakePin), wakeUp, CHANGE);
	attachInterrupt(digitalPinToInterrupt(wakePin2), wakeUp2, CHANGE);

	// Define when alarm will wake up arduino
	// First line make alarm trigger every full hour
	// Second line make alarm trigger every hour but when it is 30 minutes past full hour
	// Third line make alarm trigger every minute
	// See technical spec for ds3231
	//________________________________________________________
	//clock.setAlarm1(0, 0, 30, 0, DS3231_MATCH_M_S);
	//clock.setAlarm2(0, 0, 00, DS3231_MATCH_M);
	clock.setAlarm1(0, 0, 0, 0, DS3231_MATCH_S);
	//________________________________________________________

	Serial.println("...+++++++++...7");
	gsmSerial.println(F("ATE1"));		//Switch on Echo
	delay(500);
	ReadGsmBuffer();
	gsmSerial.println(F("AT+CMGF=1"));	// put SMS module into Text mode
	delay(500);
	ReadGsmBuffer();
	gsmSerial.println(F("AT+CIPSHUT"));	//close the GPRS PDP context
	delay(500);
	ReadGsmBuffer();
	gsmSerial.println(F("AT+CSCLK=1"));	//prepare for sleep mode when gsmWakePin is High
	delay(500);
	ReadGsmBuffer();

	// Test sensors and clock
	// TODO: Finish tis debugging section
	dt = clock.getDateTime();
	sprintf(print_date, "%02d/%02d/%d %02d:%02d:%02d", dt.day, dt.month, dt.year, dt.hour, dt.minute, dt.second);
	Serial.println(print_date);
	voltage = readBattery();
	ReadAtmospherics();
	ReadSoil();
	readWeight(10);
	Serial.println("Current humidity: " + String(currentHumidity) + "%");
	Serial.println("Current temperature: " + String(currentTemperature) + "C");
	Serial.println("Current temperature: " + String(bmeTemperature) + "C");
	Serial.println("Current pressure: " + String(bmePressure) + "mbr");
	Serial.println("Current humid: " + String(bmeHumid) + "%");
	Serial.println("Soil status: " + String(ReadSoil()) + "%");
	Serial.println("Battery status: " + String(voltage) + "%");
	Serial.println("Wieght: " + String(weight));

}

void loop()
{
	//Put SIM800l into sleep mode
	gsmSerial.println(F("AT+CSCLK=1"));	//prepare for sleep mode when gsmWakePin is High
	delay(1000);
	ReadGsmBuffer();
	digitalWrite(gsmWakePin, HIGH);
	delay(500);
	scale.power_down();		// Put scale in sleep mode
	delay(200);

	Serial.println("go to sleep...");
	delay(100);
	Serial.println("...+++++++++...");
	delay(100);

	goToSleep();

	//-----------------------------
	//ZzZzZzZzZzZzZzZzZzZzZzZzZzZzZzZzZz
	//-----------------------------

	// point of wakeing up arduino

	delay(1000);
	scale.power_up(); //Awake scale
	digitalWrite(6, HIGH);
	//while (digitalRead(3) == HIGH)
	//{
		delay(1000);
	//}
	digitalWrite(6, LOW);

	PurgeGsmBuffer();
	delay(200);

	// Display the date and time
	dt = clock.getDateTime();
	sprintf(print_date, "%02d/%02d/%d %02d:%02d:%02d", dt.day, dt.month, dt.year, dt.hour, dt.minute, dt.second);
	Serial.println(print_date);
	if (dt.hour == 0 && reseted == false)
	{
		Serial.println("midnight");
		scale.tare();
		reseted = true;
	}
	else if (dt.hour != 0 && reseted == true)
	{
		reseted = false;
	}

	voltage = readBattery();
	ReadSoil();
	ReadAtmospherics();
	readWeight(10);
	gsmSerial.println(weight);

	Upload();						// upload data to ThingSpeak

	PurgeGsmBuffer();				// delete leftovers from gsm buffer
	delay(2000);
}

// function to delete GSM buffer
void PurgeGsmBuffer()
{
	while (gsmSerial.available() > 0)
	{
		gsmSerial.read();
	}
}
// function to upload data on thingSpeak IoT site
void Upload()
{
	Serial.println(F("Uploading:..."));
	gsmSerial.println(F("AT+CREG?"));
	delay(1000);
	ReadGsmBuffer();
	gsmSerial.println(F("AT+CGATT?"));
	delay(1000);
	ReadGsmBuffer();
	gsmSerial.println(F("AT+CIPSHUT"));
	delay(1000);
	ReadGsmBuffer();
	gsmSerial.println(F("AT+CIPSTATUS"));
	delay(1000);
	ReadGsmBuffer();
	gsmSerial.println(F("AT+CIPMUX=0"));
	delay(1000);
	ReadGsmBuffer();
	gsmSerial.println(F("AT + CSTT = \"internet\",\"\",\"\""));
	delay(1000);
	ReadGsmBuffer();
	gsmSerial.println(F("AT+CIICR"));
	delay(1000);
	ReadGsmBuffer();
	gsmSerial.println(F("AT+CIFSR"));
	delay(1000);
	gsmSerial.println(F("AT+CIPSPRT=0"));
	delay(1000);
	ReadGsmBuffer();
	gsmSerial.println(F("AT+CIPSTART=\"TCP\",\"api.thingspeak.com\",\"80\""));
	delay(2000);
	ReadGsmBuffer();
	gsmSerial.println(F("AT+CIPSEND"));
	delay(2000);
	ReadGsmBuffer();
	gsmSerial.println(thingSpeakUpadate + "&field1=" + String(currentTemperature) + "&field2=" + String(currentHumidity) + "&field3=" + String(voltage) + "&field4=" + String(bmeTemperature) + "&field5=" + String(bmePressure) + "&field6=" + String(bmeHumid) + "&field7=" + String(weightInGrams) + "&field8=" + String(soilMoisture));
	delay(2000);
	ReadGsmBuffer();
	gsmSerial.println(String(char(26)));
	delay(1000);
	ReadGsmBuffer();
	gsmSerial.println();
	gsmSerial.println(F("AT+CIPSHUT"));
	delay(5000);
	ReadGsmBuffer();
	Serial.println(F("Finished uploading!"));
	delay(1000);
}
// function to read response from GSM module
void ReadGsmBuffer()
{
	while (gsmSerial.available())
	{
		Serial.write(gsmSerial.read());
	}
}
// function to invoke sleep mode to arduino uno
void goToSleep()
{
	cli();
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);		// setting the sleep mode
	sleep_bod_disable();
	sei();
	sleep_cpu();
	power_usart0_disable();
	power_timer0_disable();
	power_timer1_disable();
	power_timer2_disable();
	//power_twi_disable();
	//power_spi_disable();
	sleep_enable();								// enables the sleep bit in the mcucr register
	sleep_mode();

	// The Arduino wake up here
	delay(100);
	clock.clearAlarm1();						// Clear the DS3231 alarm (ready for the next triggering)
	clock.clearAlarm2();						// Clear the DS3231 alarm (ready for the next triggering)
}

// ISR (Interrupt Service Routine) function to wake up the Arduino
void wakeUp()
{
	//detachInterrupt(digitalPinToInterrupt(wakePin));
	//detachInterrupt(digitalPinToInterrupt(wakePin2));
	sleep_disable();							// Just after wake up, disable the sleeping mode
	power_all_enable();
	//delay(2000);

	digitalWrite(gsmWakePin, LOW);
	scale.power_up(); //Awake scale
}
void wakeUp2()
{
	sleep_disable();							// Just after wake up, disable the sleeping mode
	power_all_enable();
	//delay(2000);
	//detachInterrupt(0);
	//detachInterrupt(1);
	digitalWrite(gsmWakePin, LOW);
}
float ReadSoil()
{
	analogReference(DEFAULT);
	analogRead(A3);
	delay(200);
	soilReading = analogRead(A3); //reads the sensor value
	soilMoisture = ((1023 - soilReading) / 1023) * 100;
	analogReference(INTERNAL);
	analogRead(A3);
	return soilMoisture;
}
float readBattery()
{
	voltage = 0;
	for (int i = 1; i <= 10; i++)
	{
		float battValue = analogRead(A0);
		voltage = voltage + battValue;
		delay(200);
		}
	voltage = voltage / 10;
	voltage = voltage / 1023 * 1100;
	//Serial.println((float)voltage, 3);
	//map min and max voltage values on analog pin to scale o% to 100%
	/*
	For clarification:
	Analog reading on defined analog pin A0 is measured based on internal voltage of 1.1V
	If voltage on the pin is 1.1V, arduino will measure 1023 value.
	If voltage on pin iz 0 arduino will measure 0 value.

	BUT!!!

	LiIon battery can have voltage values between about 4.4V(full charge) and about 3V (empty)

	SO!!!

	We have to scale input voltage on that pin to be at maximum 1.1V when battery is full.
	It can be done with voltage divider (see schematics)
	With battery I use maximum value on the pin is 1035mV (on voltage divider), and I consider battery empty when value falls to 800)

	*/
	voltage = map(voltage, 860, 1035, 0, 100);
	if (voltage > batteryMax)
	{
		voltage = batteryMax;
	}
	else
	{
		batteryMax = voltage;
	}

	return voltage;
}
int ReadAtmospherics()
{
	//Serial.write("2");
	dht.read(DHTPIN);								// read current temperature and humidity
	currentTemperature = dht.temperature;			// set temperature readings in temperature array
	currentHumidity = dht.humidity;					// set humidity readings in humidity array

	if (!bme.beginI2C())
	{
		Serial.println(F("Could not find a valid BMP280 sensor, check wiring!"));
		//while (1);
	}
	bmeTemperature =bme.readTempC();
	bmePressure = (bme.readFloatPressure() / 100);			// 100 Pa = 1 millibar
	bmeAltitude = (bme.readFloatAltitudeMeters());			//we assume that atmosferic pressure at sea level is 1013.27mb
	bmeHumid = (bme.readFloatHumidity());
}
float readWeight(int loops)
{
	// for resetting purposes becouse scale have error measurement after big weight change
	//for (int i = 0; i < loops; i++)
	//{
	//	weight = weight + scale.get_units(), 3;
	//	//Serial.println(weight);
	//	delay(100);
	//}
	weight = 0;
	for (int i = 0; i < loops; i++)
	{
		weight = weight + scale.get_units(), 3;
		//Serial.println(weight);
		delay(100);
	}
	Serial.println(weight);
	weight = weight / loops;
	Serial.println(weight);
	weightInGrams=weight*1000;
	Serial.println(weightInGrams);

	scale.tare();
	return 	weightInGrams;
}