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

class TransportUserImpl : public SIP::Passenger {
private:
    string StopName = "";
public:
    void assignStopLocation(string location) {
        StopName = location;
    }

    void updateTramInfo(shared_ptr<TramPrx> tram, StopList stops, const Ice::Current &current) override {
        cout << "Tram update: " << tram->getStockNumber() << endl;
        cout << "Upcoming stops:" << endl;

        for (const auto &stop: stops) {
            cout << "- " << stop.stop->getName() << " at time "
                 << stop.time.hour << ":" << stop.time.minute << endl;
        }
    }

    void updateStopInfo(shared_ptr<TramStopPrx> tramStop, TramList tramList, const Ice::Current &current) override {
        for (size_t i = 0; i < tramList.size(); ++i) {
            TramInfo tramInfo = tramList[i];
            cout << "Tram number: " << tramInfo.tram->getStockNumber()
                 << "\t Arrival time: " << tramInfo.time.hour << ":" << tramInfo.time.minute << endl;
        }
    };

    void notifyPassenger(string info, const Ice::Current &current) override {
        cout << info << endl;
    }
};

int main(int argc, char *argv[]) {
    string serverAddress = "";
    string serverPort = "";
    string serviceName = "";
    string clientPort = argv[1];

    ifstream configData("configfile.txt");
    if (configData.is_open()) {
        string configLine;
        while (getline(configData, configLine)) {
            istringstream lineStream(configLine);
            string configKey, configValue;

            if (getline(lineStream, configKey, '=') && getline(lineStream, configValue)) {
                configKey.erase(remove_if(configKey.begin(), configKey.end(), ::isspace), configKey.end());
                configValue.erase(remove_if(configValue.begin(), configValue.end(), ::isspace), configValue.end());

                if (configKey == "address") {
                    serverAddress = configValue;
                } else if (configKey == "port") {
                    serverPort = configValue;
                } else if (configKey == "name") {
                    serviceName = configValue;
                }
            }
        }
        configData.close();
    } else {
        cerr << "ERROR: UNABLE TO OPEN CONFIGURATION FILE" << endl;
        return 1;
    }

    if (serverAddress.empty() || serverPort.empty() || serviceName.empty()) {
        cerr << "ERROR: MISSING REQUIRED CONFIGURATION PARAMETERS" << endl;
        cerr << "REQUIRED PARAMETERS: address, port, name" << endl;
        return 1;
    }

    cout << "Enter your username: " << endl;
    string userName;
    cin >> userName;
    std::cin.clear();

    Ice::CommunicatorPtr communicator;
    try {
        communicator = Ice::initialize(argc, argv);
        auto proxy = communicator->stringToProxy(serviceName + ":default -h " + serverAddress + " -p " + serverPort + " -t 8000");
        auto transportService = Ice::checkedCast<MPKPrx>(proxy);
        if (!transportService) {
            throw "ERROR: INVALID PROXY";
        }

        std::string clientIp;
        std::ifstream ipFile("selfip.txt");
        if (ipFile.is_open()) {
            std::getline(ipFile, clientIp);
            ipFile.close();
            cout << "Using IP address: " << clientIp << endl;
        } else {
            std::cerr << "ERROR: CANNOT OPEN IP ADDRESS FILE. USING DEFAULT IP." << std::endl;
            clientIp = "127.0.0.1";
        }

        std::string endpoint = "tcp -h " + clientIp + " -p " + clientPort;
        Ice::ObjectAdapterPtr adapter = communicator->createObjectAdapterWithEndpoints("ClientAdapter", endpoint);

        auto passenger = make_shared<TransportUserImpl>();
        auto passengerProxy = Ice::uncheckedCast<PassengerPrx>(adapter->addWithUUID(passenger));
        adapter->add(passenger, Ice::stringToIdentity(userName));

        LineList lines = transportService->getLines();

        StopList allStops;
        cout << "Available lines: " << endl << endl;

        for (size_t index = 0; index < lines.size(); ++index) {
            cout << "----Line number: " << lines[index]->getName()<<" ----" << endl;
            StopList tramStops = lines[index]->getStops();

            cout << "Total stops: " << tramStops.size() << endl;
            cout << "Stops: " << endl;

            for (size_t stopIndex = 0; stopIndex < tramStops.size(); stopIndex++) {
                shared_ptr<TramStopPrx> tramStop = tramStops[stopIndex].stop;
                cout << ">> " << tramStop->getName() << endl;

                bool stopExists = false;
                for (size_t indexAllStop = 0; indexAllStop < allStops.size(); indexAllStop++) {
                    if (allStops[indexAllStop].stop->getName() == tramStop->getName()) {
                        stopExists = true;
                        break;
                    }
                }

                if (!stopExists) {
                    StopInfo stopInfo;
                    stopInfo.stop = tramStop;
                    allStops.push_back(stopInfo);
                }
            }

            TramList trams = lines[index]->getTrams();
            cout << "|- Avaliable Trams on this line: -|" << endl;
            for (size_t tramIndex = 0; tramIndex < trams.size(); tramIndex++) {
                cout <<"Tram number: " << trams[tramIndex].tram->getStockNumber() << endl;
            }
            cout << endl;
        }

        cout << "Available stops: " << endl;
        for (size_t stopIndex = 0; stopIndex < allStops.size(); stopIndex++) {
            shared_ptr<TramStopPrx> tramStop = allStops[stopIndex].stop;
            cout << ">> " << tramStop->getName() << endl;

            TramList fullTramList;
            int fetchSize = 5;
            int totalFetched = 0;

            while (true) {
                TramList batch = tramStop->getNextTrams(totalFetched + fetchSize);
                if (batch.size() == fullTramList.size()) {
                    break;
                }
                fullTramList = batch;
                totalFetched += fetchSize;
            }

            passenger->updateStopInfo(tramStop, fullTramList, Ice::Current());
        }

        cout << endl ;

        char subscriptionType;
        string subscriptionName;
        cout << "Choose what to subscribe to: 's' - stop, 't' - tram" << endl;
        cin >> subscriptionType;

        if (subscriptionType == 's') cout << "Enter stop name: " << endl;
        else if (subscriptionType == 't') cout << "Enter tram number: " << endl;
        else throw "ERROR: INVALID SUBSCRIPTION CHOICE";

        cin >> subscriptionName;

        adapter->activate();
        shared_ptr<TramPrx> tram = nullptr;
        shared_ptr<TramStopPrx> tramStop = nullptr;
        char userCommand;

        if (subscriptionType == 's') {
            tramStop = transportService->getTramStop(subscriptionName);
            if (tramStop) {
                tramStop->RegisterPassenger(passengerProxy);
                passenger->assignStopLocation(subscriptionName);
                cout << "You are now subscribed to stop: " << subscriptionName << endl;
                while (true) {
                }
            } else {
                throw "ERROR: STOP NOT FOUND";
            }
        }
        else {
            for (size_t index = 0; index < lines.size(); index++) {
                TramList trams = lines[index]->getTrams();
                for (size_t tramIndex = 0; tramIndex < trams.size(); tramIndex++) {
                    if (trams[tramIndex].tram->getStockNumber() == subscriptionName) {
                        tram = trams[tramIndex].tram;
                        break;
                    }
                }
                if (tram) break;
            }

            if (tram) tram->RegisterPassenger(passengerProxy);
            else cout <<"ERROR: TRAM NUMBER NOT FOUND" << endl;

            cout << "Press 'q' to exit the program" << endl;
            cout << "Press 'n' to see upcoming stops" << endl;

            while (true) {
                cin >> userCommand;
                if (userCommand == 'n') {
                    cout << "Enter number of stops to display: " << endl;
                    int stopCount;
                    cin >> stopCount;
                    StopList nextStops = tram->getNextStops(stopCount);
                    passenger->updateTramInfo(tram, nextStops, Ice::Current());
                }
                if (userCommand == 'q') break;
            }
        }

        if (subscriptionType == 's') {
            cout << "Unregistering from stop" << endl;
            tramStop->UnregisterPassenger(passengerProxy);
        } else {
            cout << "Unregistering from tram" << endl;
            tram->UnregisterPassenger(passengerProxy);
        }

    } catch (const Ice::Exception &e) {
        cout << "ERROR: " << e << endl;
    } catch (const char *msg) {
        cout << msg << endl;
    }

    if (communicator) {
        try {
            communicator->destroy();
        } catch (const Ice::Exception &e) {
            cout << "ERROR: " << e << endl;
        }
    }

    cout << "END" << endl;
}