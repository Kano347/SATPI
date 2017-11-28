/* Server.cpp

   Copyright (C) 2015 - 2017 Marc Postema (mpostema09 -at- gmail.com)

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
   Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */
#include <upnp/ssdp/Server.h>

#include <InterfaceAttr.h>
#include <Properties.h>
#include <StringConverter.h>
#include <Log.h>
#include <Utils.h>

#include <chrono>
#include <thread>

//#include <stdio.h>
//#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace upnp {
namespace ssdp {

#define UTIME_DEL 200000

Server::Server(const InterfaceAttr &interface,
               Properties &properties) :
	ThreadBase("SSDP Server"),
	_interface(interface),
	_properties(properties) {
}

Server::~Server() {
	cancelThread();
	joinThread();
	// we should send bye bye
	sendByeBye(_properties.getDeviceID(), _properties.getUUID().c_str());
}

void Server::threadEntry() {
	std::string fileBootID = _properties.getAppDataPath() + "/" + "bootID";
	_properties.setBootID(readBootIDFromFile(fileBootID.c_str()));

	SI_LOG_INFO("Setting up SSDP server with BOOTID: %d", _properties.getBootID());

	// Get file and constuct new location
	std::string location;
	std::string xmlDeviceDescriptionFile = _properties.getXMLDeviceDescriptionFile();
	location = StringConverter::stringFormat("http://%1:%2/%3",
		_interface.getIPAddress(),
		_properties.getHttpPort(),
		xmlDeviceDescriptionFile);

	initUDPSocket(_udpMultiSend, SSDP_PORT, "239.255.255.250");
	initMutlicastUDPSocket(_udpMultiListen, "239.255.255.250", SSDP_PORT, _interface.getIPAddress().c_str());

	std::time_t repeat_time = 0;
	struct pollfd pfd[1];
	pfd[0].fd = _udpMultiListen.getFD();
	pfd[0].events = POLLIN | POLLHUP | POLLRDNORM | POLLERR;
	pfd[0].revents = 0;

	for (;; ) {
		// call poll with a timeout of 500 ms
		const int pollRet = poll(pfd, 1, 500);
		if (pollRet > 0) {
			if (pfd[0].revents != 0) {
				struct sockaddr_in si_other;
				socklen_t addrlen = sizeof(si_other);
				const ssize_t size = recvfromHttpcMessage(_udpMultiListen, MSG_DONTWAIT, &si_other, &addrlen);

				if (size > 0) {
					char ip_addr[25];
					// save client ip address
					strcpy(ip_addr, (char *)inet_ntoa(si_other.sin_addr));

					// @TODO we should probably listen to only one message
					// check do we hear our echo, same UUID
					std::string usn("None");
					StringConverter::getHeaderFieldParameter(_udpMultiListen.getMessage(), "USN:", usn);
					if (!(usn.size() > 5 && usn.compare(5, _properties.getUUID().size(), _properties.getUUID()) == 0)) {
						// get method from message
						std::string method;
						if (StringConverter::getMethod(_udpMultiListen.getMessage(), method)) {
							if (method.compare("NOTIFY") == 0) {
								std::string param;
								if (StringConverter::getHeaderFieldParameter(_udpMultiListen.getMessage(), "DEVICEID.SES.COM:", param) &&
								    usn.find("SatIPServer:1") != std::string::npos) {
									// get device id of other
									const unsigned int otherDeviceID = atoi(param.c_str());

									// check server found with clashing DEVICEID? we should defend it!!
									if (_properties.getDeviceID() == otherDeviceID) {
										SI_LOG_INFO("Found SAT>IP Server %s: with clashing DEVICEID %d defending", ip_addr, otherDeviceID);
										SocketClient udpSend;
										initUDPSocket(udpSend, SSDP_PORT, ip_addr);
										// send message back
										const char *UPNP_M_SEARCH =
										        "M-SEARCH * HTTP/1.1\r\n" \
										        "HOST: %1:%2\r\n" \
										        "MAN: \"ssdp:discover\"\r\n" \
										        "ST: urn:ses-com:device:SatIPServer:1\r\n" \
										        "USER-AGENT: Linux/1.0 UPnP/1.1 SatPI/%3\r\n" \
										        "DEVICEID.SES.COM: %4\r\n" \
										        "\r\n";
										const std::string msg = StringConverter::stringFormat(UPNP_M_SEARCH,
											_interface.getIPAddress(),
											SSDP_PORT,
											_properties.getSoftwareVersion(),
											_properties.getDeviceID());
										if (!udpSend.sendDataTo(msg.c_str(), msg.size(), 0)) {
											SI_LOG_ERROR("SSDP M_SEARCH data send failed");
										}
									} else {
										SI_LOG_INFO("Found SAT>IP Server %s: with DEVICEID %d", ip_addr, otherDeviceID);
									}
								}
							} else if (method.compare("M-SEARCH") == 0) {
								std::string param;
								const char *UPNP_M_SEARCH_OK =
								        "HTTP/1.1 200 OK\r\n" \
								        "CACHE-CONTROL: max-age=%1\r\n" \
								        "EXT:\r\n" \
								        "LOCATION: %2\r\n" \
								        "SERVER: Linux/1.0 UPnP/1.1 SatPI/%3\r\n" \
								        "ST: urn:ses-com:device:SatIPServer:1\r\n" \
								        "USN: uuid:%4::urn:ses-com:device:SatIPServer:1\r\n" \
								        "BOOTID.UPNP.ORG: %5\r\n" \
								        "CONFIGID.UPNP.ORG: 0\r\n" \
								        "DEVICEID.SES.COM: %6\r\n" \
								        "\r\n";
								if (StringConverter::getHeaderFieldParameter(_udpMultiListen.getMessage(), "DEVICEID.SES.COM:", param)) {
									// someone contacted us, so this should mean we have the same DEVICEID
									SI_LOG_INFO("SAT>IP Server %s: contacted us because of clashing DEVICEID %d", ip_addr, _properties.getDeviceID());

									// send message back
									const std::string msg = StringConverter::stringFormat(UPNP_M_SEARCH_OK,
										_properties.getSsdpAnnounceTimeSec(),
										location,
										_properties.getSoftwareVersion(),
										_properties.getUUID(),
										_properties.getBootID(),
										_properties.getDeviceID());
									if (sendto(_udpMultiSend.getFD(), msg.c_str(), msg.size(), 0, (struct sockaddr *)&si_other, sizeof(si_other)) == -1) {
										PERROR("send");
									}
									// we should increment DEVICEID and send bye bye
									_properties.setDeviceID(_properties.getDeviceID() + 1);
									sendByeBye(_properties.getDeviceID(), _properties.getUUID().c_str());

									// now increment bootID
									_properties.setBootID(readBootIDFromFile(fileBootID.c_str()));
									SI_LOG_INFO("Changing BOOTID to: %d", _properties.getBootID());

									// reset repeat time to annouce new DEVICEID
									repeat_time =  std::time(nullptr) + 5;
								}
								std::string st_param;
								if (StringConverter::getHeaderFieldParameter(_udpMultiListen.getMessage(), "ST:", st_param)) {
									if (st_param.compare("urn:ses-com:device:SatIPServer:1") == 0) {
										// client is sending a discover
										SI_LOG_INFO("SAT>IP Client %s : tries to discover the network, sending reply back", ip_addr);

										// send message back
										const std::string msg = StringConverter::stringFormat(UPNP_M_SEARCH_OK,
											_properties.getSsdpAnnounceTimeSec(),
											location,
											_properties.getSoftwareVersion(),
											_properties.getUUID(),
											_properties.getBootID(),
											_properties.getDeviceID());
										if (sendto(_udpMultiSend.getFD(), msg.c_str(), msg.size(), 0, (struct sockaddr *)&si_other, sizeof(si_other)) == -1) {
											PERROR("send");
										}
									}
								}
							}
						}
					}
				}
			}
		}

		// Did the Device Description File change?
		if (xmlDeviceDescriptionFile != _properties.getXMLDeviceDescriptionFile()) {
			// Get new file and constuct new location
			xmlDeviceDescriptionFile = _properties.getXMLDeviceDescriptionFile();
			location = StringConverter::stringFormat("http://%1:%2/%3",
				_interface.getIPAddress(),
				_properties.getHttpPort(),
				xmlDeviceDescriptionFile);

			// we should send bye bye
			sendByeBye(_properties.getDeviceID(), _properties.getUUID().c_str());

			// now increment bootID
			_properties.setBootID(readBootIDFromFile(fileBootID.c_str()));
			SI_LOG_INFO("Changing BOOTID to: %d", _properties.getBootID());

			// reset repeat time to annouce new 'Device Description File'
			repeat_time =  std::time(nullptr) + 5;
		}

		// Notify/announce ourself
		const std::time_t curr_time = std::time(nullptr);
		if (repeat_time < curr_time) {
			// set next announce time
			repeat_time = _properties.getSsdpAnnounceTimeSec() + curr_time;
			{
				// broadcast message
				const char *UPNP_ROOTDEVICE =
						"NOTIFY * HTTP/1.1\r\n" \
						"HOST: 239.255.255.250:1900\r\n" \
						"CACHE-CONTROL: max-age=%1\r\n" \
						"LOCATION: %2\r\n" \
						"NT: upnp:rootdevice\r\n" \
						"NTS: ssdp:alive\r\n" \
						"SERVER: Linux/1.0 UPnP/1.1 SatPI/%3\r\n" \
						"USN: uuid:%4::upnp:rootdevice\r\n" \
						"BOOTID.UPNP.ORG: %5\r\n" \
						"CONFIGID.UPNP.ORG: 0\r\n" \
						"DEVICEID.SES.COM: %6\r\n" \
						"\r\n";
				const std::string msg = StringConverter::stringFormat(UPNP_ROOTDEVICE,
					_properties.getSsdpAnnounceTimeSec(),
					location,
					_properties.getSoftwareVersion(),
					_properties.getUUID(),
					_properties.getBootID(),
					_properties.getDeviceID());
				if (!_udpMultiSend.sendDataTo(msg.c_str(), msg.size(), 0)) {
					SI_LOG_ERROR("SSDP UPNP_ROOTDEVICE data send failed");
				}
			}
			std::this_thread::sleep_for(std::chrono::microseconds(UTIME_DEL));
			{
				// broadcast message
				const char *UPNP_ALIVE =
						"NOTIFY * HTTP/1.1\r\n" \
						"HOST: 239.255.255.250:1900\r\n" \
						"CACHE-CONTROL: max-age=%1\r\n" \
						"LOCATION: %2\r\n" \
						"NT: uuid:%3\r\n" \
						"NTS: ssdp:alive\r\n" \
						"SERVER: Linux/1.0 UPnP/1.1 SatPI/%4\r\n" \
						"USN: uuid:%3\r\n" \
						"BOOTID.UPNP.ORG: %5\r\n" \
						"CONFIGID.UPNP.ORG: 0\r\n" \
						"DEVICEID.SES.COM: %6\r\n" \
						"\r\n";
				const std::string msg = StringConverter::stringFormat(UPNP_ALIVE,
					_properties.getSsdpAnnounceTimeSec(),
					location,
					_properties.getUUID(),
					_properties.getSoftwareVersion(),
					_properties.getBootID(),
					_properties.getDeviceID());
				if (!_udpMultiSend.sendDataTo(msg.c_str(), msg.size(), 0)) {
					SI_LOG_ERROR("SSDP UPNP_ALIVE data send failed");
				}
			}
			std::this_thread::sleep_for(std::chrono::microseconds(UTIME_DEL));
			{
				// broadcast message
				const char *UPNP_DEVICE =
						"NOTIFY * HTTP/1.1\r\n" \
						"HOST: 239.255.255.250:1900\r\n" \
						"CACHE-CONTROL: max-age=%1\r\n" \
						"LOCATION: %2\r\n" \
						"NT: urn:ses-com:device:SatIPServer:1\r\n" \
						"NTS: ssdp:alive\r\n" \
						"SERVER: Linux/1.0 UPnP/1.1 SatPI/%3\r\n" \
						"USN: uuid:%4::urn:ses-com:device:SatIPServer:1\r\n" \
						"BOOTID.UPNP.ORG: %5\r\n" \
						"CONFIGID.UPNP.ORG: 0\r\n" \
						"DEVICEID.SES.COM: %6\r\n" \
						"\r\n";
				const std::string msg = StringConverter::stringFormat(UPNP_DEVICE,
					_properties.getSsdpAnnounceTimeSec(),
					location,
					_properties.getSoftwareVersion(),
					_properties.getUUID(),
					_properties.getBootID(),
					_properties.getDeviceID());
				if (!_udpMultiSend.sendDataTo(msg.c_str(), msg.size(), 0)) {
					SI_LOG_ERROR("SSDP UPNP_DEVICE data send failed");
				}
			}
		}
	}
}

bool Server::sendByeBye(unsigned int bootId, const char *uuid) {
	{
		// broadcast message
		const char *UPNP_ROOTDEVICE_BB =
				"NOTIFY * HTTP/1.1\r\n" \
				"HOST: 239.255.255.250:1900\r\n" \
				"NT: upnp:rootdevice\r\n" \
				"NTS: ssdp:byebye\r\n" \
				"USN: uuid:%1::upnp:rootdevice\r\n" \
				"BOOTID.UPNP.ORG: %2\r\n" \
				"CONFIGID.UPNP.ORG: 0\r\n" \
				"\r\n";
		const std::string msg = StringConverter::stringFormat(UPNP_ROOTDEVICE_BB, uuid, bootId);
		if (!_udpMultiSend.sendDataTo(msg.c_str(), msg.size(), 0)) {
			SI_LOG_ERROR("SSDP UPNP_ROOTDEVICE_BB data send failed");
			return false;
		}
	}
	std::this_thread::sleep_for(std::chrono::microseconds(UTIME_DEL));
	{
		// broadcast message
		const char *UPNP_BYEBYE =
				"NOTIFY * HTTP/1.1\r\n" \
				"HOST: 239.255.255.250:1900\r\n" \
				"NT: uuid:%1\r\n" \
				"NTS: ssdp:byebye\r\n" \
				"USN: uuid:%1\r\n" \
				"BOOTID.UPNP.ORG: %2\r\n" \
				"CONFIGID.UPNP.ORG: 0\r\n" \
				"\r\n";
		const std::string msg = StringConverter::stringFormat(UPNP_BYEBYE, uuid, bootId);
		if (!_udpMultiSend.sendDataTo(msg.c_str(), msg.size(), 0)) {
			SI_LOG_ERROR("SSDP BYEBYE data send failed");
			return false;
		}
	}
	std::this_thread::sleep_for(std::chrono::microseconds(UTIME_DEL));
	{
		// broadcast message
		const char *UPNP_DEVICE_BB =
				"NOTIFY * HTTP/1.1\r\n" \
				"HOST: 239.255.255.250:1900\r\n" \
				"NT: urn:ses-com:device:SatIPServer:1\r\n" \
				"NTS: ssdp:byebye\r\n" \
				"USN: uuid:%1::urn:ses-com:device:SatIPServer:1\r\n" \
				"BOOTID.UPNP.ORG: %2\r\n" \
				"CONFIGID.UPNP.ORG: 0\r\n" \
				"\r\n";
		const std::string msg = StringConverter::stringFormat(UPNP_DEVICE_BB, uuid, bootId);
		if (!_udpMultiSend.sendDataTo(msg.c_str(), msg.size(), 0)) {
			SI_LOG_ERROR("SSDP UPNP_DEVICE_BB data send failed");
			return false;
		}
	}
	std::this_thread::sleep_for(std::chrono::microseconds(UTIME_DEL));
	return true;
}

unsigned int Server::readBootIDFromFile(const char *file) {
	// Get BOOTID from file, increment and save it again
	unsigned int bootId = 0;
	int fd = open(file, O_RDWR | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH);
	if (fd > 0) {
		char content[50];
		const char *BOOTID_STR = "bootID=%ul";
		if (read(fd, content, sizeof(content)) >= 0) {
			if (strlen(content) != 0) {
				sscanf(content, BOOTID_STR, &bootId);
				lseek(fd, 0, SEEK_SET);
			}
		} else {
			PERROR("Unable to read file: bootID");
		}
		++bootId;
		sprintf(content, BOOTID_STR, bootId);
		if (write(fd, content, strlen(content)) == -1) {
			PERROR("Unable to write file: bootID");
		}
		CLOSE_FD(fd);
	} else {
		PERROR("Unable to open file: bootID");
	}
	return bootId;
}

} // namespace ssdp
} // namespace upnp
