// simple-battery-voltage-alarm Version 1.18
// Makes alarm sound in the terminal while battery voltage is out of range,
// and produces some statistic information. This program can only run on
// tablet or notebook computers with Linux installed.

// Copyright (C) 2021 wuwbobo2021 <wuwbobo@outlook.com>
// GitHub Repository: <https://github.com/wuwbobo2021/simple-battery-voltage-alarm>

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <cmath>
#include <thread>

#include "pthread.h"
#include "unistd.h"
#include <sys/stat.h>
#include <sys/types.h>
#include "dirent.h"

using namespace std;

bool file_readable(string filepath){
	int r = access(filepath.c_str(), F_OK); //check for existence
	if (r != 0) return false;
	
	r = access(filepath.c_str(), R_OK); //check for reading permission
	return (r == 0);
}

string float_str(float f, unsigned int precision = 3, bool showpos = false){
	static stringstream sstr; //not on stack
	sstr.str(""); //clear buffer
	sstr.setf(ios::fixed); sstr.precision(precision);
	if (showpos)
		sstr.setf(ios::showpos);
	else
		sstr.unsetf(ios::showpos);
	sstr << f;
	//stringsttream::str() returns a string object with a copy of contents in the stream buffer
	return sstr.str();
}

string time_str(time_t time, bool underline = false){
	// here's C time operations, see C reference
	// some of them can not be replaced by C++ <chrono> functions
	string strformat = underline? "%Y-%m-%d_%H_%M_%S" : "%Y-%m-%d %H:%M:%S";
	tm* pstructtime; char cstrtime[20];
	
	pstructtime = localtime(&time);
	strftime(cstrtime, 20, strformat.c_str(), pstructtime);
	string str = cstrtime; //implicit cast
	return str;
}

string difftime_str(time_t time){
	int t = (int)time;
	int h = t / 3600; t %= 3600;
	int m = t / 60; t %= 60;
	string str = ((h < 10)? "0":"") + to_string(h) + ':'
	           + ((m < 10)? "0":"") + to_string(m) + ':'
	           + ((t < 10)? "0":"") + to_string(t);
	return str;
}

bool askyn(){
	static char s[1024];
	s[0] = '\0';
	cin.getline(s, 1024);
	return tolower(s[0]) == 'y';
}

void cpause() {askyn();}

struct power_reading { //sizeof per record (on 64-bit platforms): 32 B
	time_t time;
	bool charging; bool full;
	float voltage; float E;
	float current; //reference direction is the direction of charging
	int capacity; //remaining, -1 if unknown
	
	bool outofrange; //a tag, reader cannot decide it
	
	power_reading();
	power_reading(time_t t, bool c, bool f, float v, float a, float e, int cp = -1);

	float power();
	string usrstr(bool withstatus = true);
	operator const string();
};

power_reading::power_reading(): outofrange(false) {};
power_reading::power_reading(time_t t, bool c, bool f, float v, float a, float e, int cp):
	time(t), charging(c), full(f), voltage(v), current(a), E(e), capacity(cp), outofrange(false) {}
	
// absorbed power of the battery.
// but in cases of 'manualswitch' and 'charging', it is the power of computer circuit (minus).
float power_reading::power(){
	if (current >= 0) //charging, voltage > E
		return voltage * current;
	else //discharging, E > voltage. in case of manualswitch and charging: E = voltage 
		return E * current;
}
	
string power_reading::usrstr(bool withstatus){
	return time_str(time) + " "
		+ (withstatus? (charging? (full? "Full ":"Charging "):"Discharging ") : " ")
		+ ((capacity >= 0)? to_string(capacity) + "%, " : "")
		+ float_str(voltage) + " V"
		+ ((E == voltage)? "" : (" (E: " + float_str(E) + " V)")) + ", "
		+ float_str(current) + " A, "
		+ float_str(voltage * current) + " W" + (outofrange? "   !":"") + "\n";
}
	
power_reading::operator const string(){ // implicit cast to const std::string
	return this->usrstr(true);
}

class power_status_reader {
	string devicepath; bool manualswitch; float ir;
	string statuspath, voltagepath, currentpath, capacitypath;
	float maxv; string tech;
	bool invalid;
	
	float freadvalue(string filepath);
	string freadstring(string filepath);
public:
	bool charging; //allows manual setting in special conditions
	
	power_status_reader(bool m, float r);
	operator const bool() const;
	
	power_reading read();
	float maxvoltage();
	string technology();
};

float power_status_reader::freadvalue(string filepath){
	if (! file_readable(filepath)) return 0;
	
	double val;
	ifstream isf(filepath.c_str());
	isf >> val;
	return val; //implicit destruction of isf
}
	
string power_status_reader::freadstring(string filepath){
	if (!file_readable(filepath)) return "";
	
	string str;
	ifstream isf(filepath.c_str());
	isf >> str;
	return str;
}

power_status_reader::power_status_reader(bool m, float r):
	manualswitch(m), ir(r) {
	
	//find device path in /sys/class/power_supply
	devicepath = "";
	string fpath = "/sys/class/power_supply/";
	string gname, spath;
	if (access(fpath.c_str(), F_OK) == 0){ //this path exists
		DIR* dr;
		struct dirent* p;
		dr = opendir(fpath.c_str());
		while (p = readdir(dr)){ //get the pointer of dirent info of the next item, break if NULL
			gname = p->d_name; //transfer to C++ string
			if (gname == "." || gname == ".."); //skip
			else {
				spath = fpath + gname + "/voltage_now";
				if (access(spath.c_str(), F_OK) == 0){ //file exists
					devicepath = fpath + gname + '/'; //device path is known
					break;
				}
			}
		}
	}
	if (devicepath == "") {invalid = true; return;}
	
	statuspath = voltagepath = currentpath = capacitypath = devicepath;
	statuspath += "status";
	voltagepath += "voltage_now";
	currentpath += "current_now";
	capacitypath += "capacity";
	
	if (!   (file_readable(statuspath)
		  && file_readable(voltagepath)
		  && file_readable(currentpath)))
		invalid = true;
	else {
		if (! manualswitch)
			this->read(); //correct value of 'charging'
		else
			charging = false;
	}
	invalid = false;
	
	string techpath = devicepath + "technology";
	if (! file_readable(techpath))
		tech = "";
	else
		tech = freadstring(techpath); 
	
	string maxvpath = devicepath + "voltage_max_design";
	if (file_readable(maxvpath)){
		maxv = freadvalue(maxvpath)/1000/1000;
	} else {
		if (tech.length() >= 6 && tech.substr(0, 6) == "Li-ion")
			maxv = 4.35; //polymer?
		else //almost impossible?
			maxv = 5; //of no use
	}
}
	
power_status_reader::operator const bool() const{
	return (! invalid);
}
	
power_reading power_status_reader::read(){
	if (invalid) return power_reading(); //return empty reading
	
	bool full = false;
	if (! manualswitch){
		char f = freadstring(statuspath).c_str()[0];
		if (tolower(f) == 'f') //first char of 'Full'
			charging = full = true;
		else
			charging = (tolower(f) == 'c'); //first char of 'Charging'
	}
	
	float u = freadvalue(voltagepath)/1000/1000;
	float i = freadvalue(currentpath)/1000/1000; //reference direction is the direction of charging
	
	float e; //actually E can't be calculated because charging current is unknown
	if (manualswitch && charging) e = u;
	else e = u + (-i * ir); // reference direction -i is that of discharging
	
	int cp = -1;
	if (! manualswitch) cp = freadvalue(capacitypath);
	
	return power_reading(time(NULL), charging, full, u, i, e, cp);
}
	
float power_status_reader::maxvoltage(){
	return maxv;
}
	
string power_status_reader::technology(){
	return tech;
}

struct poweralarmconfig{
	bool manualswitch = false; // It should be true in case of the power gauge don't know if it is charging,
							   // and the current equals that of computer circuit.
	float ir; //internal resistance
	float minvoltage;
	float maxvoltage;
	float maxpower;
	
	poweralarmconfig();
	void reset();
	string usrstr();
};

poweralarmconfig::poweralarmconfig() {reset();}
void poweralarmconfig::reset(){
	ir = 0.1; minvoltage = 3.8; maxvoltage = 4.1; maxpower = 5;
}
	
string poweralarmconfig::usrstr(){
	string str = "";
	str += "Manual Switch: ";
	str += (manualswitch? "Enabled\n":"Disabled\n");
	str += "Internal Resistance: " + float_str(ir) + " ???\n"
		 + "Proper Range: \n"
         + "  Min Voltage: " + float_str(minvoltage) + " V\n"
         + "  Max Voltage: " + float_str(maxvoltage) + " V\n"
         + "  Max Power: " + float_str(maxpower) + " W\n";
	return str;
}

ostream& operator<< (ostream& os, poweralarmconfig& c){ // & means pass by reference
	os.setf(ios::fixed); os.precision(3);
	os << "[PowerAlarmConfig]\nManualSwitch = " << c.manualswitch << "\nInternalResistance = "
	   << c.ir << "\nMinVoltage = " << c.minvoltage << "\nMaxVoltage = " << c.maxvoltage
	   << "\nMaxPower = " << c.maxpower << '\n';
	return os;
}
istream& operator>> (istream& is, poweralarmconfig& c){
	string tmp;
	is >> tmp;
	if (tmp == "[PowerAlarmConfig]"){ //merely usable, because of the spaces left and right of '='
		is >> tmp >> tmp >> c.manualswitch
		   >> tmp >> tmp >> c.ir
		   >> tmp >> tmp >> c.minvoltage
		   >> tmp >> tmp >> c.maxvoltage
		   >> tmp >> tmp >> c.maxpower;
	} else {
		is.clear(ios::badbit);
	}
	return is;
}

// default working folder will be ~ ($HOME) after chdir by getconfig(),
// while ofstream::open(char*) and system(char*) are being called
const string config_entry = "simple-battery-voltage-alarm";
const string config_filename = "version_1_18.conf";
const string stat_filename = "stat.log";

string working_folder;
poweralarmconfig config;

bool setconfig(){
	power_status_reader testrd(false, 0);
	if (! testrd) {
		cout << "Sorry: Failed to find device file. Maybe this program don't support your computer.\n";
		return false;
	}
	
	cout << "simple-battery-voltage-alarm Version 1.18\n"
		 << "\tThis program checks for battery voltage and makes "
		 << "alarm sound when the voltage (indicating current capacity) is out of proper range.\n"
		 << "\tRequirement: driver support of your model of fuel gauge (PMIC) "
		 << "included in your Linux distribution (Ubuntu should have no problem).\n\n";
	
	working_folder = getenv("HOME") + (string)"/.config";
	if (! file_readable(working_folder)){
		mkdir(working_folder.c_str(), S_IRWXU); //rwx------, see man 7 inode
		cout << "\tDirectory " << working_folder << " created.\n";
	}
	working_folder += '/' + config_entry;
	if (! file_readable(working_folder)){
		mkdir(working_folder.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); //rwxrwxr-x
		cout << "\tDirectory " << working_folder <<  " created.\n";
	}
	chdir(working_folder.c_str());
	
	cout.setf(ios::fixed); cout.precision(3);
	
	cout << "\tHas your battery charge circuit been fixed in a way which made the battery(s) "
		 << "get charged immediately from the adapter (which is modified to output a lower voltage)"
		 << "and the power gauge can not get the charging status? (Y/n) ";
	config.manualswitch = askyn();
	if (config.manualswitch)
		cout << "\tNotice: the power gauge might have been giving out wrong percentages, "
			 << "because charging current don't flow through it (the current is always consuming current). "
			 << "you can edit /etc/UPower/UPower.conf in Recovery mode "
			 << "if you have encountered the auto power-off problem.\n";
	
	string tech = testrd.technology(); float maxvol = testrd.maxvoltage();	
	if (tech != "") cout << "\tBattery technology: " << tech << '\n';
	if (tech == "Li-ion")
	     cout << "\tIt might be 18650 in a notebook, or polymer in a tablet.\n";
	else cout << "\tThis program is made for Li-ion batteries, "
	          << "it might be improper for your kind of battery.\n";
	if (maxvol > 0) cout << "\tDesigned Max Voltage: " << maxvol << " V\n";
	cout << '\n';
		
	// a DC method of measuring internal resistance, not very accurate.
	// reference formula: U1 = E - I1*r, U2 = E - I2*r.
	float U1, I1, U2, I2, r; //reference directions of I1 and I2 here are in the direction of discharging
	power_reading tmprecord;
	cout << "\tWe'll measure the internal resitance of the battery.\n";
	if (config.manualswitch){
		cout << "\tPlease make sure you're Discharging, then press Enter to continue...";
		cpause();
	}
	tmprecord = testrd.read(); U1 = tmprecord.voltage; I1 = -tmprecord.current;
	cout << "\tSample 1: " << U1 << " V, " << I1 << " A.\n";
			
	cout << "\tPlease do somthing to make the current change"
		 << (config.manualswitch? " (but make sure it is Discharging)" : "")
		 << ", then press Enter to continue...";
	cpause();
	
	tmprecord = testrd.read(); U2 = tmprecord.voltage; I2 = -tmprecord.current;
	cout << "\tSample 2: " << U2 << " V, " << I2 << " A.\n";
	
	if (abs(I1 - I2) < 0.001)
		cout << "\tSorry, the current has not changed, r was set to default: " << config.ir << " ???.\n";
	else {
		r = (U2 - U1)/(I1 - I2);
		cout << "\tr: " << r << " ???. do you think it's right value? (Y/n) ";
		if (askyn())
			config.ir = r;
		else
			cout << "\tr was set to default: " << config.ir << " ???.\n";
	}
	cout << '\n';
	
	float tmpminv, tmpmaxv, tmpmaxp;
	cout << "\tPlease input Min voltage (V, alarm while lower than this): ";
	cin >> tmpminv;
	cout << "\tMax voltage (V, not very well for the battery if higher): ";
	cin >> tmpmaxv;
		
	if (! config.manualswitch)
		cout << "\tPower of battery can be calculated, which is minus while discharging.\n";
	else
		cout << "\tDischarge power of battery can be calculated, "
		     << "and power of computer circuit can be calculated while charging.\n";
	cout << "\tMax power (W, absolute): ";
	cin >> tmpmaxp;
	
	if (! cin) {
		cout << "\n\tSorry, at least one of them is not numeric. "
			 << "Default values will be used: " << config.minvoltage << '~'
			 << config.maxvoltage << " V, " << config.maxpower << " W.\n";
		cin.clear(); cin.ignore(numeric_limits<int>::max(), '\n');
	} else {config.minvoltage = tmpminv; config.maxvoltage = tmpmaxv; config.maxpower = tmpmaxp;}
		
	if (file_readable(config_filename))
		remove(config_filename.c_str()); //delete damaged config
	ofstream ofs(config_filename);
	if (ofs){
		ofs << config << endl; //save config
		cout << "\n\tConfig saved successfully.\n\n";
	}
	
	return true;
}

bool getconfig(){
	bool needconfig = false;
	
	working_folder = (string)getenv("HOME") + "/.config/" + config_entry;
	chdir(working_folder.c_str());
	if (! file_readable(config_filename)) needconfig = true; //config file not found
	else{
		ifstream ifs(config_filename);
		if (! (ifs >> config)) needconfig = true; //config file damaged
		else
			cout << working_folder << '/' << config_filename <<" found:\n" << config.usrstr() << "\n"
			     << "you can reconfigure the program (recalculate internal resistance) by adding parameter -c.\n";
		if (ifs.is_open()) ifs.close();
	}
	
	if (needconfig) return setconfig();
	else return true;
}

//inputloop() will set these tags for checkloop() to read
volatile bool tagexit = false, tagcharging = false, tagsavelog = false; 

void checkloop(){
	const int check_interval = 5; // 5 seconds
	
	power_status_reader reader(config.manualswitch, config.ir); //creates reader
	if (! reader) {cout << "Error: Failed to read power status. Press Ctrl+D or Input 'e' to end program... "; return;}

	vector<power_reading> readings;
	float maxW = 0, Wh = 0, mAh = 0, rWh = 0; int otimes = 0; //times of out-of-range readings
	
	//current reading, actually discharging (i < 0), seconds between last two readings
	power_reading creading; bool actuald; time_t dtime;
	
	bool first = true; bool pcharging; //first loop, previous status
	while (true){
		if (config.manualswitch) reader.charging = tagcharging;
		creading = reader.read();
		
		creading.outofrange = (   creading.voltage < config.minvoltage
							   || creading.E > config.maxvoltage
							   || creading.voltage > reader.maxvoltage()
							   || abs(creading.power()) > config.maxpower);
		if (creading.outofrange) {
			bool actuald = !creading.charging || (!config.manualswitch && creading.current < 0);
			if  ((actuald && creading.voltage < config.minvoltage)
			  || (creading.voltage > reader.maxvoltage())
			  || (creading.charging && creading.E > config.maxvoltage)
			  || abs(creading.power()) > config.maxpower)
				cout << '\a'; //make alarm sound
		}
		
		if (! first){
			dtime = difftime(creading.time, readings.back().time); //back() returns last element
			if (dtime > check_interval * 5) dtime = check_interval * 5; // the program had suspended
			
			//both should be positive if it is charging, or minus if it is discharging
			Wh += readings.back().power() * dtime/3600; 
			mAh += readings.back().current * 1000 * dtime/3600;
			
			// calculate energy wasted on internal resistance, always positive
			if (!config.manualswitch || !pcharging) //in case of 'manualswitch', rWh can be calculated when discharging
				rWh += abs((readings.back().E - readings.back().voltage) * readings.back().current) * dtime/3600;
			
			// conditions of clearing record: the vector have taken 4 MB of memory,
			// status changed, the program had suspended in sleeping mode, or the program will end.
			if  (   readings.size() >= 0x20000
				 || creading.charging != pcharging
				 || dtime == check_interval * 5
				 || tagexit)
			{
				if (readings.size() >= 5){ //make statistics
					if (config.manualswitch){
						power_reading p15 = readings[readings.size() - 2 - 1]; //read 15 seconds before
						for (int i = 1; i <= 2; i++) // last two readings can be removed
							if (abs(readings.back().voltage - p15.voltage) >= 0.1){
								//such difference should be caused by manual switch delay
								if (readings.back().outofrange) otimes -= 1; // it doesn't count
								readings.pop_back();
							}
					}
					
					time_t span = difftime(readings.back().time, readings.front().time); //(s)
					int poutrange = otimes*1.0/readings.size() * 100;
				
					float dE = readings.back().E - readings.front().E; int dcapacity;
					if (! config.manualswitch) //percentage of battery remaining capacity is available
						dcapacity = readings.back().capacity - readings.front().capacity;
				
					// W is the charge power of battery, or (minus) discharge power of battery.
					// but in case of 'manualswitch' and charging, W is (minus) power of computer circuit
					float W = Wh*3600.0/span;
					float rW; if (!config.manualswitch || !pcharging) rW = rWh*3600.0/span;
					float CWh; if (pcharging) CWh = Wh - rWh;
					float esfullWh; float esfullmAh;
					if (!config.manualswitch && dcapacity >= 5){ //changed at least 5%
						if (pcharging)
							esfullWh = CWh * 100 / dcapacity;
						else
							esfullWh = Wh * 100 / dcapacity;
						esfullmAh = mAh * 100 / dcapacity;
					}
					
					string strstat;
					strstat = (pcharging? "Charged for ":"Discharged for ") + difftime_str(span) + ", ";
					if (! config.manualswitch)
						strstat += to_string(dcapacity) + "% ("
						         + to_string(readings.front().capacity) + "% -> "
						         + to_string(readings.back().capacity) + "%), ";
					strstat += float_str(dE, 3, true) + " V ("
					         + float_str(readings.front().E) + " V -> "
					         + float_str(readings.back().E) + " V)\n"
					         + time_str(readings.front().time) + " ~ " + time_str(readings.back().time)
					         + " (out of range in " + to_string(poutrange) + "% of time)\n";
					if (!config.manualswitch || !pcharging)
						strstat += "Average Power of Battery: " + float_str(W) + " W (Max: " + float_str(maxW) + " W)    "
						         + "Pr: " + float_str(rW) + " W\n"
						         + "Charged: " + float_str((Wh > 0)? CWh : Wh, 3, true) + " Wh ("
						         + float_str(mAh, 0, true) + " mAh)\n";
					else
						strstat += "Power of Computer Circuit: " + float_str(abs(W)) + " W\n"
						         + "Energy cost by Computer Circuit: " + float_str(abs(Wh)) + " Wh ("
						         + float_str(abs(mAh), 0) + " mAh)\n";
					if (!config.manualswitch && dcapacity >= 5)
						strstat += "Full Capacity Estimation: " + float_str(esfullWh) + " Wh ("
						           + float_str(esfullmAh, 0) + " mAh)\n";
					
					cout << '\n' << strstat << '\n';
				
					ofstream ofsstat(stat_filename, ios::app); //create or append	
					if (ofsstat){
						ofsstat << strstat << endl;
						ofsstat.close();
						cout << "appended to log file " << working_folder << '/' << stat_filename << ".\n";
					}
					if (tagsavelog){ //save complete log
						string log_filename = (pcharging? "Charging_" : "Discharging_")
						                    + time_str(time(NULL),true) + ".log";
						ofstream ofslog(log_filename);
						if (ofslog){
							ofslog << strstat << '\n';
							for (int i = 0; i < readings.size(); i++)
								ofslog << readings[i].usrstr(false);
							ofslog << endl;
							ofslog.close();
							cout << "log file " << working_folder << '/' << log_filename << " saved.\n";
						}
					}
					if (! tagexit) cout << '\n';
				}  else cout << '\n';
				
				// swap with a empty temp vector that will be destructed implicitly to release memory usage
				vector<power_reading>().swap(readings);
				
				maxW = 0; Wh = 0; mAh = 0; rWh = 0; otimes = 0;
				if (tagexit) return;
			}
		} else first = false;
		
		cout << creading.usrstr();
		
		if (abs(creading.power()) > abs(maxW)) maxW = creading.power();
		if (creading.outofrange) otimes++;
		
		//add an item. if previous readings were cleared, it makes sure the vector has at least one item
		readings.push_back(creading);
		
		pcharging = creading.charging;
		sleep(check_interval); //5 seconds
	}
}

void inputloop(){
	string str;
	
	cout << "press Ctrl+D or input 'e' to end the program, input 'l' to enable/disable complete log saving";
	if (config.manualswitch)
		cout << ", input 'c'(charging) or 'd'(discharging) to set charging status (nessesary, it should be right after you plug in or pull out the charge line).\n"
			 << "Notice: to avoid disturbing, this program determines "
			 << "whether or not to make alarm sound by your manual status setting.\n\n";
	else cout << ".\n\n";
	
	while (cin >> str){
		char f = tolower(str.c_str()[0]);
		switch (f){
			case 'e':
				tagexit = true; break;
			case 'c':
				if (config.manualswitch) tagcharging = true;
				break;
			case 'd':
				if (config.manualswitch) tagcharging = false;
				break;
			case 'l':
				tagsavelog = ! tagsavelog;
				cout << "Log Saving " << (tagsavelog? "Enabled" : "Disabled") << ".\n";
		}
	}
	
	tagexit = true; // Ctrl+D pressed, input ended
}

int main(int argc, char* argv[]){
	// processing command parameters
	bool reconfig = false;
	if (argc > 1){
		char c;
		for (int i = 1; i < argc; i++){
			if (argv[1][0] == '-' && strlen(argv[i]) == 2){
				c = argv[i][1];
				switch (c){
					case 'l':
						tagsavelog = true; break;
					case 'c':
						reconfig = true; break;
					case 'h':
						cout << "-l\tEnable log saving\n" << "-c\tReconfigure\n";
						return 0;
				}
			}
		}
	
	}
	
	if (reconfig){ // -c parameter was found
		if (! setconfig()) return 1;
	} else {
		if (! getconfig()) return 1;
	}
	
	thread threadinput(inputloop); //wait for input
	threadinput.detach(); //now the thread object can be destroyed, but the thread will continue
	checkloop();
	return 0;
}
