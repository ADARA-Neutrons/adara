The STS production code base is ~temporarily~ being developed as a Qt desktop
project. (It will be converted to an autoconf/makefile setup eventually.)
This project uses files from the SMS project - which must be checked out in
an adjacent directoryto sts-prod. The plan is to create a new top-level
ADARA project with subprojects fort the SMS, STS, and other apps/utils/
test code.

If working on this code, Qt Creator will autogenerate an sts.pro.user file
and Makefile when the project is opened (you must select target settings).
Please don't check these files in as they differ on every development
machine. (Not sure how to get around this...)

The STS production code is not yet finished (see todo.txt) and is completely
undocumented...  :)

