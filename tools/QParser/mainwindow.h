#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <../../common/ADARAParser.h>
#include <../../common/ADARA.h>

#include <QMainWindow>
#include <QDataStream>
#include <QFile>
#include <QMap>

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow, public ADARA::Parser
{
  Q_OBJECT
  
public:
  explicit MainWindow(QWidget *parent = 0);
  ~MainWindow();
  
private slots:
  void openFile();
  void go();  // slot for the "Go" button - exactly what happens depends on some other settings
  void step();  // Step through the input file one packet at a time
  void resetInput();

private:

  // Packet parsing function (only need the generic one - don't need
  // functions specific to each packet type)
  bool rxPacket(const ADARA::Packet &pkt);



  void displayPacket(); // Generic packet display - prints header info and a hex dump


  void displayPacketHeader( const ADARA::PacketHeader &hdr);  // helper for the above displayPacket() function(s)

  Ui::MainWindow *ui;

  int m_in;   // Posix file descriptor because that's what the packet parser expects for now
  bool m_haveReadPacket;  // used to help test for eof (because file descriptors don't have explicit eof tests)

  QFile m_stdout; // for now, output to stdout.  In the future, this may become a QIODevice pointer
                  // that could point to a file (stdout) or maybe a TCP socket...

  const ADARA::Packet *m_pkt;  // holds onto the packet so we can send it out after looking at it on the screen

  QMap< int, ADARA::PacketType::Enum> m_comboMap;  // Maps indexes from the packet type combo to actual ADARA packet types

  // The following flags control the behavior of the packet parser
  bool m_displayPacket;  // Parse & display the packet on the screen
  bool m_keepReading;  // whether or not to break out of the packet processing loop
};

#endif // MAINWINDOW_H



