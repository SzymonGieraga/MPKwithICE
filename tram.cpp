#include <Ice/Ice.h>
#include "MPK.h"
#include <iostream>
#include <memory>
#include <fstream>
#include <string>
#include <algorithm>
#include <sstream>

using namespace std;
using namespace SIP;

class TramI : public SIP::Tram {
private:
    TramStatus status;
    string stockNumber;
    shared_ptr<TramStopPrx> currentStop;
    StopList stopList;
    vector<shared_ptr<PassengerPrx>> passengers;
    shared_ptr<LinePrx> line;
    std::shared_ptr<TramPrx> selfPrx;

public:
    TramI(string stockNumber) : stockNumber(stockNumber), status(SIP::TramStatus::OFFLINE) {}

    void addStop(const struct StopInfo stopInfo) {
        stopList.push_back(stopInfo);
    }

    void setProxy(std::shared_ptr<TramPrx> prx) {
        selfPrx = prx;
    }

    void setNextStop() {
        if (!line) return;

        auto stops = line->getStops();
        for (size_t i = 0; i < stops.size(); ++i) {
            if (currentStop->getName() == stops.at(i).stop->getName()) {
                currentStop->removeCurrentTram(selfPrx);

                size_t nextIndex = (i + 1 < stops.size()) ? i + 1 : 0;
                currentStop = stops.at(nextIndex).stop;
                currentStop->addCurrentTram(selfPrx);

                notifyPassengersOfArrival();
                return;
            }
        }
    }

    void notifyPassengersOfArrival() {
        if (passengers.empty()) return;

        string info = "Tram " + stockNumber + " has arrived at " + currentStop->getName();
        for (auto& passenger : passengers) {
            passenger->notifyPassenger(info, Ice::Context());
        }
    }

    void setLine(shared_ptr<LinePrx> line, const Ice::Current&) override {
        this->line = line;
        this->currentStop = this->line->getStops().at(0).stop;
    }

    shared_ptr<LinePrx> getLine(const Ice::Current&) override {
        return line;
    }

    shared_ptr<TramStopPrx> getLocation(const Ice::Current&) override {
        return currentStop;
    }

    int getNextStopIndex(const StopList& allStops) {
        for (size_t i = 0; i < allStops.size(); i++) {
            if (allStops.at(i).stop->getName() == currentStop->getName()) {
                return (i + 1 < allStops.size()) ? i + 1 : -1;
            }
        }
        return -1;
    }

    StopList getNextStops(int howMany, const Ice::Current&) override {
        StopList nextStops;
        if (!line) return nextStops;

        StopList allStops = line->getStops();
        int stopIndex = getNextStopIndex(allStops);

        if (stopIndex != -1) {

            for (int i = stopIndex; i < stopIndex + howMany && i < allStops.size(); ++i) {
                nextStops.push_back(allStops.at(i));
            }

            for (int i = stopIndex - 2; nextStops.size() < howMany && i >= 0; --i) {
                nextStops.push_back(allStops.at(i));
            }
        } else {
            for (size_t i = 0; nextStops.size() < howMany && i < allStops.size() - 1; ++i) {
                nextStops.push_back(allStops.at(i));
            }
        }

        return nextStops;
    }

    void informPassenger(shared_ptr<TramPrx> tram, StopList stops) {
        for (auto& passenger : passengers) {
            passenger->updateTramInfo(tram, stops);
        }
    }

    void RegisterPassenger(shared_ptr<PassengerPrx> passenger, const Ice::Current&) override {
        cout << "User subscribed" << endl;
        passengers.push_back(passenger);
    }

    void UnregisterPassenger(shared_ptr<PassengerPrx> passenger, const Ice::Current&) override {
        auto it = find_if(passengers.begin(), passengers.end(),
                         [&passenger](const auto& p) {
                             return p->ice_getIdentity() == passenger->ice_getIdentity();
                         });

        if (it != passengers.end()) {
            cout << "User unsubscribed" << endl;
            passengers.erase(it);
        }
    }

    string getStockNumber(const Ice::Current&) override {
        return stockNumber;
    }

    TramStatus getStatus(const Ice::Current&) override {
        return status;
    }

    void setStatus(TramStatus status, const Ice::Current&) override {
        this->status = status;
    }
};

int getIdLine(const LineList& lines, const string& name) {
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines.at(i)->getName() == name) {
            return i;
        }
    }
    return -1;
}

bool checkName(const string& line_name, const LineList& lines) {
    return any_of(lines.begin(), lines.end(),
                 [&line_name](const auto& line) {
                     return line_name == line->getName();
                 });
}

void displayAvailableLines(const LineList& lines) {
    cout << "Available lines: " << endl << endl;
    for (const auto& line : lines) {
        cout << "Line number: " << line->getName() << endl << "Stops: " << endl;

        StopList tramStops = line->getStops();
        for (const auto& stop : tramStops) {
            cout << "\t" << stop.stop->getName() << endl;
        }
        cout << endl << endl;
    }
}

bool readConfigFile(string& address, string& port, string& name) {
    ifstream configFile("configfile.txt");
    if (!configFile.is_open()) {
        cerr << "UNABLE TO OPEN CONFIGFILE.TXT" << endl;
        return false;
    }

    string line;
    while (getline(configFile, line)) {
        istringstream iss(line);
        string key, value;

        if (getline(iss, key, '=') && getline(iss, value)) {
            key.erase(remove_if(key.begin(), key.end(), ::isspace), key.end());
            value.erase(remove_if(value.begin(), value.end(), ::isspace), value.end());

            if (key == "address") {
                address = value;
            } else if (key == "port") {
                port = value;
            } else if (key == "name") {
                name = value;
            }
        }
    }
    configFile.close();

    if (address.empty() || port.empty() || name.empty()) {
        cerr << "MISSING REQUIRED CONFIGURATION PARAMETERS IN CONFIGFILE.TXT" << endl;
        cerr << "REQUIRED PARAMETERS: address, port, name" << endl;
        return false;
    }
    return true;
}

string readIPAddress() {
    string ipAddress;
    ifstream ipFile("selfip.txt");

    if (ipFile.is_open()) {
        getline(ipFile, ipAddress);
        ipFile.close();
        cout << "Using IP: " << ipAddress << endl;
    } else {
        cerr << "CANNOT OPEN SELFIP.TXT. USING DEFAULT IP." << endl;
        ipAddress = "127.0.0.1";
    }

    return ipAddress;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        cerr << "USAGE: " << argv[0] << " <tramPort e.g. 10010>" << endl;
        return 1;
    }

    string tramPort = argv[1];
    string address, port, name;

    if (!readConfigFile(address, port, name)) {
        return 1;
    }

    Ice::CommunicatorPtr ic;
    try {
        ic = Ice::initialize(argc, argv);
        auto base = ic->stringToProxy(name + ":default -h " + address + " -p " + port + " -t 8000");
        auto mpk = Ice::checkedCast<MPKPrx>(base);

        if (!mpk) {
            throw "INVALID PROXY";
        }

        LineList lines = mpk->getLines();
        displayAvailableLines(lines);

        string ipAddress = readIPAddress();
        string endpoint = "tcp -h " + ipAddress + " -p " + tramPort;
        Ice::ObjectAdapterPtr adapter = ic->createObjectAdapterWithEndpoints("TramAdapter", endpoint);

        string tramStockNumber;
        cout << "Enter your tram number: ";
        cin >> tramStockNumber;
        std::cin.clear();
        cout << endl;

        auto tram = make_shared<TramI>(tramStockNumber);
        auto tramPrx = Ice::uncheckedCast<TramPrx>(adapter->addWithUUID(tram));
        tram->setProxy(tramPrx);
        adapter->add(tram, Ice::stringToIdentity("tram" + tramStockNumber));

        string line_name;
        cout << "Choose a line by entering its name: ";
        cin >> line_name;

        while (!checkName(line_name, lines)) {
            cout << "Invalid line, choose again: " << endl;
            cin >> line_name;
        }

        std::cin.clear();

        time_t currentTime;
        time(&currentTime);
        tm *timeNow = localtime(&currentTime);

        int ID = getIdLine(lines, line_name);
        shared_ptr<LinePrx> linePrx = lines.at(ID);
        tram->setLine(linePrx, Ice::Current());

        StopList tramStops = linePrx->getStops();
        int hour = timeNow->tm_hour;
        int minute = timeNow->tm_min;
        int interval = 10;

        for (size_t index = 0; index < tramStops.size(); index++) {
            Time timeOfDay;
            timeOfDay.hour = hour;
            timeOfDay.minute = minute;

            StopInfo stopInfo;
            stopInfo.time = timeOfDay;
            stopInfo.stop = tramStops.at(index).stop;

            tram->addStop(stopInfo);
            tramStops.at(index).stop->UpdateTramInfo(tramPrx, timeOfDay);

            minute += interval;
            if (minute >= 60) {
                hour++;
                minute = minute - 50;
            }
        }

        adapter->activate();
        linePrx->registerTram(tramPrx);
        mpk->getDepo("Zajezdnia1")->registerTram(tramPrx);

        cout << "Waiting for tram to be online..." << endl;
        while (tram->getStatus(Ice::Current()) != SIP::TramStatus::ONLINE) {
        }

        cout << "Enter 'q' to quit the program. Enter 'n' to move to the next stop" << endl;
        char sign;
        while (true) {
            cin >> sign;
            if (sign == 'q') {
                break;
            }
            if (sign == 'n') {
                tram->setNextStop();
                cout << "You've arrived at the next stop: " << tramPrx->getLocation()->getName() << endl;
            }
        }

        linePrx->unregisterTram(tramPrx);
        mpk->getDepo("Zajezdnia1")->unregisterTram(tramPrx);
        cout << "You are in the depot, waiting for tram to go offline..." << endl;

        while (tram->getStatus(Ice::Current()) != SIP::TramStatus::OFFLINE) {
        }

    } catch (const Ice::Exception &e) {
        cout << e << endl;
    } catch (const char *msg) {
        cout << msg << endl;
    }

    if (ic) {
        try {
            ic->destroy();
        } catch (const Ice::Exception &e) {
            cout << e << endl;
        }
    }

    cout << "Tram program terminated" << endl;
    return 0;
}