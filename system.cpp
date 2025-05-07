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

class MPK_I : public SIP::MPK {
private:
    LineList all_lines;
    StopList all_stops;
    DepoList all_depos;
    vector<std::shared_ptr<LineFactoryPrx>> lineFactories;
    vector<std::shared_ptr<StopFactoryPrx>> stopFactories;

public:
    LineList getLines(const Ice::Current&) override {
        return all_lines;
    }

    void addStop(shared_ptr<TramStopPrx> tramStop) {
        StopInfo stopInfo;
        stopInfo.stop = tramStop;
        all_stops.push_back(stopInfo);
    }

    void addLine(shared_ptr<LinePrx> line, const Ice::Current&) override {
        all_lines.push_back(line);
    }

    void registerDepo(::std::shared_ptr<DepoPrx> depo, const Ice::Current&) override {
        cout << "New depot: " << depo->getName() << endl;
        DepoInfo depoInfo;
        depoInfo.stop = depo;
        depoInfo.name = depo->getName();
        all_depos.push_back(depoInfo);
    }

    void unregisterDepo(::std::shared_ptr<DepoPrx> depo, const Ice::Current&) override {
        auto it = find_if(all_depos.begin(), all_depos.end(),
                         [&depo](const DepoInfo& info) {
                             return info.stop->getName() == depo->getName();
                         });

        if (it != all_depos.end()) {
            cout << "Removing depot: " << depo->getName() << endl;
            all_depos.erase(it);
        }
    }

    shared_ptr<TramStopPrx> getTramStop(string name, const Ice::Current&) override {
        auto it = find_if(all_stops.begin(), all_stops.end(),
                         [&name](const StopInfo& info) {
                             return info.stop->getName() == name;
                         });

        return (it != all_stops.end()) ? it->stop : nullptr;
    }

    shared_ptr<DepoPrx> getDepo(string name, const Ice::Current&) override {
        auto it = find_if(all_depos.begin(), all_depos.end(),
                         [&name](const DepoInfo& info) {
                             return info.stop->getName() == name;
                         });

        return (it != all_depos.end()) ? it->stop : nullptr;
    }

    DepoList getDepos(const Ice::Current&) override {
        return all_depos;
    }

    void registerLineFactory(std::shared_ptr<SIP::LineFactoryPrx> lf, const Ice::Current&) override {
        if (std::find(lineFactories.begin(), lineFactories.end(), lf) == lineFactories.end()) {
            lineFactories.push_back(lf);
            std::cout << "LineFactory ON." << std::endl;
        }
    }

    void unregisterLineFactory(std::shared_ptr<SIP::LineFactoryPrx> lf, const Ice::Current&) override {
        auto it = std::find(lineFactories.begin(), lineFactories.end(), lf);
        if (it != lineFactories.end()) {
            lineFactories.erase(it);
            std::cout << "LineFactory OFF." << std::endl;
        }
    }

    void registerStopFactory(std::shared_ptr<SIP::StopFactoryPrx> sf, const Ice::Current&) override {
        if (std::find(stopFactories.begin(), stopFactories.end(), sf) == stopFactories.end()) {
            stopFactories.push_back(sf);
            std::cout << "StopFactory ON." << std::endl;
        }
    }

    void unregisterStopFactory(std::shared_ptr<SIP::StopFactoryPrx> sf, const Ice::Current&) override {
        auto it = std::find(stopFactories.begin(), stopFactories.end(), sf);
        if (it != stopFactories.end()) {
            stopFactories.erase(it);
            std::cout << "StopFactory OFF." << std::endl;
        }
    }
};

class TramStopI : public SIP::TramStop {
private:
    string name;
    LineList lines;
    vector<shared_ptr<PassengerPrx>> passengers;
    TramList coming_trams;
    TramList currentTrams;

public:
    TramStopI(string name) : name(name) {}

    void addLine(::std::shared_ptr<LinePrx> line) {
        lines.push_back(line);
    }

    string getName(const Ice::Current&) override {
        return name;
    }

    TramList getNextTrams(int howMany, const Ice::Current&) override {
        TramList nextTrams;
        for (int i = 0; i < howMany && i < coming_trams.size(); ++i) {
            nextTrams.push_back(coming_trams.at(i));
        }
        return nextTrams;
    }

    void RegisterPassenger(::std::shared_ptr<PassengerPrx> passenger, const Ice::Current&) override {
        passengers.push_back(passenger);
        cout << "Passenger is observing stop: " << this->name << endl;
        cout << "Stop: " << this->name << endl;
    }

    void UnregisterPassenger(::std::shared_ptr<PassengerPrx> passenger, const Ice::Current&) override {
        auto it = find_if(passengers.begin(), passengers.end(),
                         [&passenger](const auto& p) {
                             return p->ice_getIdentity() == passenger->ice_getIdentity();
                         });

        if (it != passengers.end()) {
            passengers.erase(it);
            cout << "Passenger unsubscribed from stop: " << name << endl;
        }
    }

    void UpdateTramInfo(std::shared_ptr<SIP::TramPrx> tram, SIP::Time time, const Ice::Current&) override {
        TramInfo tramInfo;
        tramInfo.tram = tram;
        tramInfo.time = time;

        for (size_t i = 0; i < coming_trams.size(); ++i) {
            if ((coming_trams.at(i).time.hour > time.hour) ||
                (coming_trams.at(i).time.hour == time.hour && coming_trams.at(i).time.minute > time.minute)) {
                coming_trams.insert(coming_trams.begin() + i, tramInfo);
                return;
            }
        }
        coming_trams.push_back(tramInfo);
    }

    void addCurrentTram(shared_ptr<SIP::TramPrx> tram, const Ice::Current&) override {
        TramInfo tramInfo;
        tramInfo.tram = tram;
        currentTrams.push_back(tramInfo);

        string header = "Trams at stop " + name;
        cout << header << endl;
        cout << "Number of subscribed passengers: " << passengers.size() << endl;

        // Notify passengers about the stop
        for (const auto& passenger : passengers) {
            passenger->notifyPassenger(header, Ice::Context());
        }

        // Notify about each tram
        for (const auto& tramInfo : currentTrams) {
            string info = "Tram: " + tramInfo.tram->getStockNumber();
            cout << info << endl;

            for (const auto& passenger : passengers) {
                passenger->notifyPassenger(info, Ice::Context());
            }
        }
    }

    void removeCurrentTram(shared_ptr<SIP::TramPrx> tram, const Ice::Current&) override {
        auto it = find_if(currentTrams.begin(), currentTrams.end(),
                         [&tram](const TramInfo& info) {
                             return info.tram->ice_getIdentity() == tram->ice_getIdentity();
                         });

        if (it != currentTrams.end()) {
            currentTrams.erase(it);
        }
    }
};

class LineI : public SIP::Line {
private:
    TramList all_trams;
    StopList all_stops;
    string name;

public:
    LineI(string name) : name(name) {}

    TramList getTrams(const Ice::Current&) override {
        return all_trams;
    }

    SIP::StopList getStops(const Ice::Current&) override {
        return all_stops;
    }

    string getName(const Ice::Current&) override {
        return name;
    }

    void registerTram(shared_ptr<TramPrx> tram, const Ice::Current&) override {
        TramInfo tramInfo;
        tramInfo.tram = tram;
        all_trams.push_back(tramInfo);
        cout << "New tram with number: " << tram->getStockNumber() << " has been added" << endl;
    }

    void unregisterTram(::std::shared_ptr<TramPrx> tram, const Ice::Current&) override {
        auto it = find_if(all_trams.begin(), all_trams.end(),
                         [&tram](const TramInfo& info) {
                             return info.tram->getStockNumber() == tram->getStockNumber();
                         });

        if (it != all_trams.end()) {
            cout << "Tram number " << tram->getStockNumber() << " is leaving the line" << endl;
            cout << "Waiting for offline " << tram->getStockNumber() << endl;
            all_trams.erase(it);
        }
    }

    void setStops(SIP::StopList sl, const Ice::Current&) override {
        all_stops = sl;
    }
};

class DepoI : public SIP::Depo {
private:
    string name;
    TramList all_trams;

public:
    DepoI(string name) : name(name) {}

    void TramOnline(::std::shared_ptr<TramPrx> tram, const Ice::Current&) override {
        if (tram) {
            tram->setStatus(SIP::TramStatus::ONLINE, Ice::Context());
            cout << "Tram " << tram->getStockNumber() << " has left the depot" << endl;
        } else {
            cout << "TRAM DOES NOT EXIST" << endl;
        }
    }

    void TramOffline(::std::shared_ptr<TramPrx> tram, const Ice::Current&) override {
        if (tram) {
            tram->setStatus(SIP::TramStatus::OFFLINE, Ice::Context());
            cout << "Tram " << tram->getStockNumber() << " has entered the depot" << endl;
        } else {
            cout << "TRAM DOES NOT EXIST" << endl;
        }
    }

    string getName(const Ice::Current&) override {
        return name;
    }

    void registerTram(::std::shared_ptr<TramPrx> tram, const Ice::Current&) override {
        TramInfo tramInfo;
        tramInfo.tram = tram;
        tramInfo.tram->setStatus(SIP::TramStatus::WAITONLINE, Ice::Context());
        all_trams.push_back(tramInfo);
        cout << "Depot registered tram number: " << tram->getStockNumber() << endl;
    }

    void unregisterTram(::std::shared_ptr<TramPrx> tram, const Ice::Current&) override {
        if (tram) {
            tram->setStatus(SIP::TramStatus::WAITOFFLINE, Ice::Context());
        }
    }

    TramList getTrams(const Ice::Current&) override {
        return all_trams;
    }
};

class LineFactoryI : public SIP::LineFactory {
private:
    int linesCreated = 0;
    Ice::ObjectAdapterPtr adapter;

public:
    LineFactoryI(Ice::ObjectAdapterPtr adapter) : adapter(adapter) {}

    std::shared_ptr<SIP::LinePrx> createLine(string name, const Ice::Current&) override {
        auto newLine = make_shared<LineI>(name);
        linesCreated++;
        auto linePrx = Ice::uncheckedCast<SIP::LinePrx>(adapter->addWithUUID(newLine));
        return linePrx;
    }

    double getLoad(const Ice::Current& = Ice::Current()) override {
        return static_cast<double>(linesCreated);
    }
};

class StopFactoryI : public SIP::StopFactory {
private:
    int stopsCreated = 0;
    Ice::ObjectAdapterPtr adapter;

public:
    StopFactoryI(Ice::ObjectAdapterPtr adapter) : adapter(adapter) {}

    std::shared_ptr<SIP::TramStopPrx> createStop(string name, const Ice::Current&) override {
        auto newStop = make_shared<TramStopI>(name);
        stopsCreated++;
        auto stopPrx = Ice::uncheckedCast<SIP::TramStopPrx>(adapter->addWithUUID(newStop));
        return stopPrx;
    }

    double getLoad(const Ice::Current& = Ice::Current()) override {
        return static_cast<double>(stopsCreated);
    }
};

// Helper functions
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

void loadStops(MPK_I* mpk, StopFactoryI* stopFactory) {
    ifstream stops_file("stops.txt");
    if (!stops_file.is_open()) {
        cerr << "CANNOT OPEN STOPS FILE." << endl;
        throw "File error";
    }

    string stop_name;
    while (stops_file >> stop_name) {
        auto tramStopPrx = stopFactory->createStop(stop_name, Ice::Current());
        mpk->addStop(tramStopPrx);
    }
}

void loadLines(MPK_I* mpk, LineFactoryI* lineFactory, StopFactoryI* stopFactory) {
    ifstream lines_file("lines.txt");
    if (!lines_file.is_open()) {
        cerr << "CANNOT OPEN LINES FILE." << endl;
        throw "File error";
    }

    string file_line;
    cout << "LINES AND STOPS: " << endl;

    while (getline(lines_file, file_line)) {
        size_t separator_position = file_line.find(':');
        if (separator_position == string::npos) continue;

        string line_number = file_line.substr(0, separator_position);
        cout << "Line: " << line_number << endl;

        auto linePrx = lineFactory->createLine(line_number, Ice::Current());
        string tramStopsNames = file_line.substr(separator_position + 1);

        istringstream iss(tramStopsNames);
        string stop_name;

        time_t currentTime;
        time(&currentTime);
        tm* timeNow = localtime(&currentTime);

        StopList stopList;
        while (iss >> stop_name) {
            auto tramStopPrx = mpk->getTramStop(stop_name, Ice::Current());
            if (!tramStopPrx) {
                tramStopPrx = stopFactory->createStop(stop_name, Ice::Current());
                mpk->addStop(tramStopPrx);
            }

            StopInfo stopInfo;
            stopInfo.time.hour = timeNow->tm_hour;
            stopInfo.time.minute = timeNow->tm_min;
            stopInfo.stop = tramStopPrx;
            stopList.push_back(stopInfo);
        }

        linePrx->setStops(stopList);
        mpk->addLine(linePrx, Ice::Current());

        istringstream ss(tramStopsNames);
        cout << "STOPS: ";
        while (ss >> stop_name) {
            shared_ptr<TramStopPrx> tramStopPrx = mpk->getTramStop(stop_name, Ice::Current());
            if (!tramStopPrx) {
                cout << "NO STOPS FOUND";
            } else {
                cout << tramStopPrx->getName() << " ";
            }
        }
        cout << endl;
    }
}

void printTramStatus(const TramStatus status) {
    switch (status) {
        case SIP::TramStatus::ONLINE:
            cout << "online" << endl;
            break;
        case SIP::TramStatus::OFFLINE:
            cout << "offline" << endl;
            break;
        case SIP::TramStatus::WAITONLINE:
            cout << "waiting for online" << endl;
            break;
        case SIP::TramStatus::WAITOFFLINE:
            cout << "waiting for offline" << endl;
            break;
        default:
            cout << "UNKNOWN STATUS" << endl;
    }
}

void depotManagement(shared_ptr<DepoPrx> depoPrx, shared_ptr<MPK_I> mpk) {
    while (true) {
        cout << "Depot: " << depoPrx->getName() << endl;

        cout << "Registered trams: " << endl;
        TramList tramList = depoPrx->getTrams(Ice::Context());

        for (size_t i = 0; i < tramList.size(); ++i) {
            cout << i << ". " << tramList.at(i).tram->getStockNumber() << " - ";
            printTramStatus(tramList.at(i).tram->getStatus(Ice::Context()));
        }

        cout << "Enter '<number> on' or '<number> off', or 'q' to exit the depot: " << endl;
        string command = "";
        cin.ignore();
        getline(cin, command);

        if (command == "q") {
            cout << "Closing depot" << endl;
            break;
        }

        istringstream iss(command);
                int number;
                string action;
                iss >> number >> action;

                if (number < 0 || number >= tramList.size()) {
                    cout << "Nieprawidlowy numer tramwaju." << endl;
                    return;
                } else if (action == "on") {
                    shared_ptr <TramPrx> tram = tramList.at(number).tram;
                    if (tram->getStatus(Ice::Context()) == SIP::TramStatus::ONLINE) {
                        cout << "Tramwaj jest juz online" << endl;
                        return;
                    } else {
                        depoPrx->TramOnline(tram, Ice::Context());
                        cout << "Tramwaj " << tram->getStockNumber() << " jest online" << endl;
                        return;
                    }
                } else if (action == "off") {
                    shared_ptr <TramPrx> tram = tramList.at(number).tram;
                    if (tram->getStatus(Ice::Context()) == SIP::TramStatus::OFFLINE) {
                        cout << "Tramwaj jest juz wylaczony" << endl;
                        return;
                    } else {
                        depoPrx->TramOffline(tram, Ice::Context());
                        cout << "Tramwaj " << tram->getStockNumber() << " jest wylaczony" << endl;
                        return;
                    }
                } else {
                    cout << "Nieznana komenda." << endl;
                }
    }
}

int main(int argc, char *argv[]) {
    Ice::CommunicatorPtr ic;
    try {
        ic = Ice::initialize(argc, argv);
        string ipAddress = readIPAddress();
        string endpoint = "default -h " + ipAddress + " -p 10000";

        Ice::ObjectAdapterPtr adapter = ic->createObjectAdapterWithEndpoints("MPKAdapter", endpoint);
        auto mpk = make_shared<MPK_I>();
        adapter->add(mpk, Ice::stringToIdentity("mpk"));

        auto depo = make_shared<DepoI>("Zajezdnia1");
        auto depoPrx = Ice::uncheckedCast<DepoPrx>(adapter->addWithUUID(depo));
        mpk->registerDepo(depoPrx, Ice::Current());

        auto lineFactory = make_shared<LineFactoryI>(adapter);
        auto lineFactoryPrx = Ice::uncheckedCast<LineFactoryPrx>(adapter->addWithUUID(lineFactory));
        mpk->registerLineFactory(lineFactoryPrx, Ice::Current());

        auto stopFactory = make_shared<StopFactoryI>(adapter);
        auto stopFactoryPrx = Ice::uncheckedCast<StopFactoryPrx>(adapter->addWithUUID(stopFactory));
        mpk->registerStopFactory(stopFactoryPrx, Ice::Current());

        loadStops(mpk.get(), stopFactory.get());
        loadLines(mpk.get(), lineFactory.get(), stopFactory.get());

        adapter->activate();

        while (true) {
            cin.ignore();
            cout << "Press 'd' to enter depot" << endl;
            char sign;
            cin >> sign;

            if (sign == 'd') {
                depotManagement(depoPrx, mpk);
            }

        }

        ic->waitForShutdown();

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

    cout << "PROGRAM TERMINATED" << endl;
    return 0;
}