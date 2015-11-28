/* Properties.cpp

   Copyright (C) 2015 Marc Postema (mpostema09 -at- gmail.com)

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
#include "Properties.h"
#include "Log.h"
#include "Utils.h"
#include "Configure.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

extern const char *satpi_version;

Properties::Properties(const std::string &uuid, const std::string &delsysString, const std::string &startPath) :
		_delsysString(delsysString),
		_uuid(uuid),
		_versionString(satpi_version),
		_startPath(startPath),
		_xSatipM3U("channellist.m3u"),
		_xmlDeviceDescriptionFile("desc.xml"),
		_bootID(1),
		_deviceID(1),
		_ssdpAnnounceTimeSec(60),
		_appStartTime(time(NULL)) {;}

Properties::~Properties() {;}

void Properties::fromXML(const std::string &xml) {
	MutexLock lock(_mutex);
	std::string element;
	if (findXMLElement(xml, "data.configdata.input1.value", element)) {
		_ssdpAnnounceTimeSec = atoi(element.c_str());
		SI_LOG_INFO("Setting SSDP annouce interval to: %d Sec", _ssdpAnnounceTimeSec);
	}
	if (findXMLElement(xml, "data.configdata.xsatipm3u.value", element)) {
		_xSatipM3U = element;
	}
	if (findXMLElement(xml, "data.configdata.xmldesc.value", element)) {
		_xmlDeviceDescriptionFile = element;
	}
}

void Properties::addToXML(std::string &xml) const {
	MutexLock lock(_mutex);
	ADD_CONFIG_NUMBER_INPUT(xml, "input1", _ssdpAnnounceTimeSec, 0, 1800);
	ADD_CONFIG_TEXT_INPUT(xml, "xsatipm3u", _xSatipM3U.c_str());
	ADD_CONFIG_TEXT_INPUT(xml, "xmldesc", _xmlDeviceDescriptionFile.c_str());
}

std::string Properties::getSoftwareVersion() const {
	MutexLock lock(_mutex);
	return _versionString;
}

std::string Properties::getUUID() const {
	MutexLock lock(_mutex);
	return _uuid;
}

std::string Properties::getDeliverySystemString() const {
	MutexLock lock(_mutex);
	return _delsysString;
}

std::string Properties::getStartPath() const {
	MutexLock lock(_mutex);
	return _startPath;
}

std::string Properties::getXSatipM3U() const {
	MutexLock lock(_mutex);
	return _xSatipM3U;
}

std::string Properties::getXMLDeviceDescriptionFile() const {
	MutexLock lock(_mutex);
	return _xmlDeviceDescriptionFile;
}

void Properties::setBootID(unsigned int bootID) {
	MutexLock lock(_mutex);
	_bootID = bootID;
}

unsigned int Properties::getBootID() const {
	MutexLock lock(_mutex);
	return _bootID;
}

void Properties::setDeviceID(unsigned int deviceID) {
	MutexLock lock(_mutex);
	_deviceID = deviceID;
}

unsigned int Properties::getDeviceID() const {
	MutexLock lock(_mutex);
	return _deviceID;
}

void Properties::setSsdpAnnounceTimeSec(unsigned int sec) {
	MutexLock lock(_mutex);
	_ssdpAnnounceTimeSec = sec;
}

unsigned int Properties::getSsdpAnnounceTimeSec() const {
	MutexLock lock(_mutex);
	return _ssdpAnnounceTimeSec;
}

time_t Properties::getApplicationStartTime() const {
	MutexLock lock(_mutex);
	return _appStartTime;
}
