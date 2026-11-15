Country flags in Pegaso
=======================

ip-country.dat   which country an IPv4 address belongs to
flags.bmp        the flags, 16x11 pixels each

Pegaso reads these two files to draw a flag next to sources, clients and servers. The
lookup is done on this computer; nothing is sent anywhere. Delete this folder and Pegaso
simply shows no flags.

Credits and licences
--------------------

IP address data: "IP to Country Lite" database by DB-IP, https://db-ip.com
    Licensed under the Creative Commons Attribution 4.0 International License
    (https://creativecommons.org/licenses/by/4.0/). IP Geolocation by DB-IP.
    The data was converted to a compact format with tools/geo/make_geo.py; it is
    otherwise unchanged. Edition: November 2026.

Flag icons: Mark James, http://www.famfamfam.com - public domain.

Country data gets old: addresses change hands. Each Pegaso release ships the edition
that was current when it was built.
