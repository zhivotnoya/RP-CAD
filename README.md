# RP-CAD
Computer Aided Dispatch for Role Playing Communities

Created by zhivotnoya.

I'm creating this program to add a computer aided dispatching element to many of the law enforcement role playing communities out there.  Right now trying to get the server bit working, but eventually I'll also have a client.

The way this will work is the server will run on a dedicated VPS/Box and the client will also run there.  The user(s) will then us some sort of SSH program to connect to the server via a primary login.  Upon login, the shell will auto-execute the client program.

Once the client is run, the user will then proceed to enter their personal user ID and pin code to access the program(s) they need.  The client will only show the user the "program(s)" that they are authorized to use, which is set by the Admin(s) of their community.  

Functionality of the client will be:
- CAD (Computer Aided Dispatch).  User will be able to create, update, and clear calls.  They will also be able to assign units to those calls.  Other added features will be an NCIC lookup, as well as DMV records search on vehicles.
- MDT (Mobile Data Terminal).  Accessible by units with on-board computers.  In theory this will be running in the SSH client by the role playing officer.  At a later date, I may look into some sort of hook to LCPDFR, LSPDFR, etc etc so that the officer doesn't need a second computer/monitor.
- CIT (Civilian Interaction Terminal).  This will provide the civilian roleplayers tools to create aliases and personas for them to use.  Such information will allow civs to enter their names, gender, race, date of birth, and criminal records.  Once the data is created, they will no longer be able to change any of it without an Admin's approval.  This heightens the roleplay as from the point of creation, only the roleplaying officers/dispatchers/judges can add to their file.  This allows any infractions incurred with that roleplay to be tracked.  So if you got a ticket last week, it will show in the system.  
- AM (Administration Menu).  Here admins can enter penal codes, run reports, and manage the community.
- CRU (Central Records Unit).  This is where the officers, civs, and dispatchers can enter their reports for the sessions they complete.  Some communities like the added "paperwork" for the immersion, some don't.  But it's here for you if you wish.  Report templates can be created by the admins in the AM.


Having all the server and client programs on the server allows for easy code updates to be processed, and therefore not having to have community members download the updates each time.  Plus coding for one system makes it compatible with any system that can run an SSH program, thereby reducing the shear amount of coding required to make available on multiple platforms.
