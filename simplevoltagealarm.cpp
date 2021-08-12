// simple-battery-voltage-alarm Version 1.10
// Make alarm sound in the terminal while battery voltage is out of range,
// and make some statistic information. This program can only run on
// tablet or notebook computers with Linux installed.
// Without any warranty.

// This is a small program, which doesn't contain hard-core algorithms,
// complicated structures, or low-level hardware operations, thus
// this code has been placed in the public domain.
// However, In the purpose of bug reporting and version identification,
// anyone who modifies the code should remain this comment and add
// a record with an email address and the modifying date included.

// to show the code properly, you can set the tab width to 4.

// Command for compiling:
// g++ <codefile> -std=c++11 -pthread -o <executablefile>

// Ver 1.00 (written by wuwbobo@outlook.com, within 2021-7-7~7-14)
// First usable version.
// Ver 1.10 (by wuwbobo@outlook.com, within 2021-8-10~8-12)
// First release on github (by wuwbobo@outlook.com). Major changes:
// 1. The method of searching power gauge device path was changed,
//    so user interaction is no longer required;
// 2. The bug of not making alarm sound while battery power is too high was fixed;
// 3. Checking for full status was considered.
// 4. Problems in config directory accessing was solved,
//    and config file format was changed in a clearer way;
// 5. A parameter '-c' for configuration was provided;
// 6. Log saving is disabled by default, but the user can enable it
//    by adding '-l' parameter while starting the program.
// 7. Memory usage limit of recorded readings was changed to 4 MB.

// Known problems:
// 1. This is a terminal program, which cannot run on startup or at background;
// 2. The program may supsend in sleeping mode, If happened, statistic information
//    may be incorrect;
// 3. The situation in which the charging line was pluged in but the battery
//    is still discharging was not well considered in efficiency calculating.
// 4. Calculation of 'mAh' don't care the internal resistance (if given).
// 5. Yet this program don't use the interface functions declared in 'power_supply.h'
//    of Linux kernel headers. (it might not be a problem)
//    see: https://www.kernel.org/doc/html/latest/power/power_supply_class.html

#include <iostream>
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

bool filereadable(string filepath){
	int r = access(filepath.c_str(), F_OK); //check for existence
	if (r != 0) return false;
	
	r = access(filepath.c_str(), R_OK); //check for reading permission
	return (r == 0);
}

string timestr(time_t time, bool underline = false){
	// here's C time operations, see C reference
	// some of them can not be replaced by C++ <chrono> functions
	string strformat = underline? "%Y-%m-%d_%H_%M_%S" : "%Y-%m-%d %H:%M:%S";
	tm* pstructtime; char cstrtime[19];
	
	pstructtime = localtime(&time);
	strftime(cstrtime, 20, strformat.c_str(), pstructtime);
	string str = cstrtime; //implicit cast
	return str;
}

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

power_reading::power_reading(){outofrange = false;}
power_reading::power_reading(time_t t, bool c, bool f, float v, float a, float e, int cp):
	time(t), charging(c), full(f), voltage(v), current(a), E(e), capacity(cp) {outofrange = false;}
	
// absorbed power of the battery.
// but in cases of 'manualswitch' and 'charging', it is the power of computer circuit (minus).
float power_reading::power(){
	if (current >= 0) //charging, voltage > E
		return voltage * current;
	else //discharging, E > voltage. in case of manualswitch and charging: E = voltage 
		return E * current;
}
	
string power_reading::usrstr(bool withstatus){
	return timestr(time) + " "
		+ (withstatus? (charging? (full? "Full ":"Charging "):"Discharging ") : " ")
		+ to_string(voltage) + " V"
		+ ((E == voltage)? "" : (" (E: " + to_string(E) + " V)")) + ", "
		+ ((capacity >= 0)? to_string(capacity) + "%, " : "")
		+ to_string(current) + " A, "
		+ to_string(voltage * current) + " W" + (outofrange? "   !":"") + "\n";
}
	
power_reading::operator const string(){ // implicit cast to const std::string
	return this->usrstr(true);
}

class power_status_reader {
	string devicepath; bool manualswitch; float ir;
	string statuspath, voltagepath, currentpath, capacitypath;
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
	if (! filereadable(filepath)) return 0;
	
	double val;
	ifstream isf(filepath.c_str());
	isf >> val;
	return val; //implicit destruction of isf
}
	
string power_status_reader::freadstring(string filepath){
	if (!filereadable(filepath)) return "";
	
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
	
	if (!   (filereadable(statuspath)
		  && filereadable(voltagepath)
		  && filereadable(currentpath)))
		invalid = true;
	else {
		if (! manualswitch)
			this->read(); //correct value of 'charging'
		else
			charging = false;
	}
	invalid = false;
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
	if (invalid) return 0;
	string path = devicepath + "voltage_max_design";
	if (! filereadable(path)) return 0;
	return freadvalue(path)/1000/1000;
}
	
string power_status_reader::technology(){
	if (invalid) return "";
	string path = devicepath + "technology";
	if (! filereadable(path)) return "";
	return freadstring(path); 
}

struct poweralarmconfig{
	bool manualswitch = false; // It's true in case of power gauge don't know if it is charging,
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
	ir = 0.1; minvoltage = 3.8; maxvoltage = 4.15; maxpower = 5;
}
	
string poweralarmconfig::usrstr(){
	string str = "";
	str += "Manual switch: ";
	str += (manualswitch? "Enabled":"Disabled");
	str += "\nInternal resistance: " + to_string(ir) + " Ω"
         + "\nMin voltage: " + to_string(minvoltage) + " V"
         + "\nMax voltage: " + to_string(maxvoltage) + " V"
         + "\nMax power: " + to_string(maxpower) + " W\n";
	return str;
}

ostream& operator<< (ostream& os, poweralarmconfig& c){ // & means pass by reference
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
const string config_filename = "version_1_10.conf";
poweralarmconfig config;

bool setconfig(){
	power_status_reader testrd(false, 0);
	if (! testrd) {
		cout << "Sorry: Failed to find device file. Maybe this program don't support your computer.\n";
		return false;
	}
	
	cout << "simple-battery-voltage-alarm Version 1.10\n"
		 << "\tThis program checks for battery voltage and makes "
		 << "alarm sound when the voltage is out of proper range.\n"
		 << "\tRequirement: driver support of your model of fuel gauge (PMIC) "
		 << "included in your Linux distribution (Ubuntu should have no problem).\n\n";
	cout << "\tConfig not found, we'll start configuration.\n\n";
	
	string tmppath = getenv("HOME") + (string)"/.config";
	if (! filereadable(tmppath)){
		mkdir(tmppath.c_str(), S_IRWXU); //rwx------, see man 7 inode
		cout << "\tDirectory " << tmppath << " created.\n";
	}
	tmppath += '/' + config_entry;
	if (! filereadable(tmppath)){
		mkdir(tmppath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); //rwxrwxr-x
		cout << "\tDirectory " << tmppath <<  " created.\n";
	}
	chdir(tmppath.c_str());
	
	char s[256]; //temp c string
	cout << "\tHas your battery charge circuit been fixed in a way which made the battery(s) "
		 << "get charged immediately from the adapter (which is modified to output a lower voltage)"
		 << "and the power gauge can not get the charging status? (Y/n) ";
	cin.getline(s, 256);
	config.manualswitch = (tolower(s[0]) == 'y');
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
		cin.getline(s, 256);
	}
	tmprecord = testrd.read(); U1 = tmprecord.voltage; I1 = -tmprecord.current;
	cout << "\tSample 1: " << U1 << " V, " << I1 << " A.\n";
			
	cout << "\tPlease do somthing to make the current change"
		 << (config.manualswitch? " (but make sure it is Discharging)" : "")
		 << ", then press Enter to continue...";
	cin.getline(s, 256);
	
	tmprecord = testrd.read(); U2 = tmprecord.voltage; I2 = -tmprecord.current;
	cout << "\tSample 2: " << U2 << " V, " << I2 << " A.\n";
	
	if (abs(I1 - I2) < 0.001)
		cout << "\tSorry, the current has not changed, r was set to default: " << config.ir << " Ω.\n";
	else {
		r = (U2 - U1)/(I1 - I2);
		cout << "\tr: " << r << " Ω. do you think it's right value? (Y/n) ";
		cin.getline(s, 256);
		if (tolower(s[0]) == 'y')
			config.ir = r;
		else
			cout << "\tr was set to default: " << config.ir << " Ω.\n";
	}
	cout << '\n';
	
	float tmpminv, tmpmaxv, tmpmaxp;
	cout << "\tPlease input Min voltage (V, alarm while lower than this): ";
	cin >> tmpminv;
	cout << "\tMax voltage (V, not very well for the battery if higher): ";
	cin >> tmpmaxv;
		
	if (! config.manualswitch)
		cout << "\tPower of battery can be calculated, which is minus when discharging.\n";
	else cout << "\tThe power of computer can be calculated as minus by this program.\n";
	cout << "\tMax power (W, absolute): ";
	cin >> tmpmaxp;
	
	if (! cin) {
		cout << "\n\tSorry, at least one of them is not numeric. "
			 << "Default values will be used: " << config.minvoltage << '~'
			 << config.maxvoltage << " V, " << config.maxpower << " W.\n";
		cin.clear(); cin.ignore(numeric_limits<int>::max(), '\n');
	} else {config.minvoltage = tmpminv; config.maxvoltage = tmpmaxv; config.maxpower = tmpmaxp;}
		
	if (filereadable(config_filename))
		remove(config_filename.c_str()); //delete damaged config
	ofstream ofs(config_filename);
	if (ofs){
		ofs << config << endl; //save config
		cout << "\tConfig saved successfully.\n\n";
	}
	
	return true;
}

bool getconfig(){
	bool needconfig = false;
	
	chdir(getenv("HOME")); chdir((".config/" + config_entry).c_str());
	if (! filereadable(config_filename)) needconfig = true; //config file not found
	else{
		ifstream ifs(config_filename);
		if (! (ifs >> config)) needconfig = true; //config file damaged
		else
			cout << getenv("PWD") << '/' << config_filename <<" found:\n" << config.usrstr() << "\n"
			     << "You can reconfigure the program (recalculate internal resistance) by adding parameter -c.\n";
		if (ifs.is_open()) ifs.close();
	}
	
	if (needconfig) return setconfig();
	else return true;
}

bool tagexit = false; bool tagcharging = false; bool tagsavelog = false; //for checkloop to read
void inputloop(){
	string str;
	
	cout << "press Ctrl+D or input 'e' to end the program, input 'l' to enable/disable log saving";
	if (config.manualswitch)
		cout << ", input 'c'(charging) or 'd'(discharging) to switch charging status (nessesary).\n"
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

void checkloop(){
	power_status_reader reader(config.manualswitch, config.ir); //creates reader
	if (! reader) {cout << "Error: Failed to read power status. Press Ctrl+D or Input 'e' to end program... "; return;}

	const int check_interval = 5; // 5 seconds
	vector<power_reading> readings;
	float Wh = 0, Ah = 0, rWh = 0; int otimes = 0; //times of out-of-range readings
	
	//current recording, actually discharging (i < 0), seconds between last two readings
	power_reading creading; bool actuald; time_t dtime;
	
	bool first = true; bool pcharging; //first loop, previous status
	while (true){
		if (config.manualswitch) reader.charging = tagcharging;
		creading = reader.read();
		
		creading.outofrange = (   creading.voltage < config.minvoltage
							   || creading.E > config.maxvoltage
							   || abs(creading.power()) > config.maxpower);
		if (creading.outofrange) {
			bool actuald = !creading.charging || (!config.manualswitch && creading.current < 0);
			if  ((actuald && creading.voltage < config.minvoltage)
			  || (creading.charging && creading.E > config.maxvoltage)
			  || abs(creading.power()) > config.maxpower)
				cout << '\a'; //make alarm sound
		}
		
		if (! first){
			dtime = difftime(creading.time, readings.back().time); //back() returns last element
			
			//both should be positive if it is charging, or minus if it is discharging
			Wh += readings.back().power() * dtime/3600; 
			Ah += readings.back().current * dtime/3600;
			
			if (! config.manualswitch) //calculate energy wasted on internal resistance, always positive
				rWh += abs((readings.back().E - readings.back().voltage) * readings.back().current) * dtime/3600;
			
			// conditions of clearing record: the vector have taken over 4 MB of memory,
			// status changed or the program will end.
			if  (   readings.capacity() >= 0x20000
				 || ((creading.charging != pcharging || tagexit) && (readings.size() > 0)) ) {
				string strstat = ""; string strch = pcharging? "Charging":"Discharging";
				
				time_t span = difftime(readings.back().time, readings.front().time);
				int poutrange = otimes*1.0/readings.size() * 100; //add 1.0 to make float quotient left of *
				int mAh = round(abs(1000*Ah));
				
				strstat += strch + " for " + to_string(span) + " seconds (out of range in "
						 + to_string(poutrange) + "% of time)\n"
						 + "from " + timestr(readings.front().time)
						 + " to " + timestr(readings.back().time) + ",\n"
						 + "Battery voltage changed from " + to_string(readings.front().E) + " V"
						 + ((! config.manualswitch) ? " (" + to_string(readings.front().capacity) + "%)" : "")
						 + " to " + to_string(readings.back().E) + " V"
						 + ((! config.manualswitch) ? " (" + to_string(readings.back().capacity) + "%)" : "")
						 + ",\n";
				
				if (! config.manualswitch){
					strstat += ((Wh > 0)? to_string(Wh - rWh) : to_string(abs(Wh)))
					         + " Wh (about " + to_string(mAh) + " mAh) "
					         + ((Wh > 0)? "Charged.\n" : "Discharged.\n");
				} else {
					strstat += to_string(abs(Wh))
					         + " Wh (about " + to_string(mAh) + " mAh) "
					         + (pcharging? "of energy spent.\n" : "Discharged.\n");
				}
				
				if (Wh < 0 || (Wh > 0 && !config.manualswitch)){ //can not calc for charging if 'manualswitch'
					int pefficiency = round((1 - rWh/Wh) * 100);
					if (pefficiency < 100)
						strstat += ("Efficiency: " + to_string(pefficiency) + "%.\n");
				}
				
				cout << '\n' << strstat;
				
				if (tagsavelog && readings.size() > 5){ // save log file
					string logfilename = strch + "_" + timestr(time(NULL),true) + ".log";
					ofstream ofs(logfilename);
					if (ofs){
						ofs << strstat << '\n';
						for (int i = 0; i < readings.size(); i++) ofs << readings[i].usrstr(false);
						ofs << endl;
						ofs.close();
						cout << "\nlog file " << getenv("PWD") << '/' << logfilename << " saved.\n";
						if (! tagexit) cout << '\n';
					}
				} else cout << '\n';
				
				// swap with a empty temp vector that will be destructed implicitly to release memory usage
				vector<power_reading>().swap(readings);
				
				Wh = 0; Ah = 0; rWh = 0; otimes = 0;
				if (tagexit) return;
			}
		} else first = false;
		
		cout << creading.usrstr();
		if (creading.outofrange) otimes++;
		
		//add an item. if previous readings are saved, it makes sure the vector has at least one item
		readings.push_back(creading);
		
		pcharging = creading.charging;
		sleep(check_interval); //5 seconds
	}
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