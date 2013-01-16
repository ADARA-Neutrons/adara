#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <../../common/ADARAParser.h>

#include <QMainWindow>
#include <QDataStream>
#include <QFile>

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
  void step();  // Step through the input file one packet at a time

private:
  void resetInput();

  // Packet parsing functions
  bool rxPacket(const ADARA::Packet &pkt);
  /*************************
  bool rxPacket(const ADARA::RawDataPkt &pkt);
  bool rxPacket(const ADARA::RTDLPkt &pkt);
  bool rxPacket(const ADARA::BankedEventPkt &pkt);
  bool rxPacket(const ADARA::BeamMonitorPkt &pkt);
  bool rxPacket(const ADARA::PixelMappingPkt &pkt);
  bool rxPacket(const ADARA::RunStatusPkt &pkt);
  bool rxPacket(const ADARA::RunInfoPkt &pkt);
  bool rxPacket(const ADARA::TransCompletePkt &pkt);
  bool rxPacket(const ADARA::ClientHelloPkt &pkt);
  bool rxPacket(const ADARA::AnnotationPkt &pkt);
  bool rxPacket(const ADARA::SyncPkt &pkt);
  bool rxPacket(const ADARA::HeartbeatPkt &pkt);
  bool rxPacket(const ADARA::GeometryPkt &pkt);
  bool rxPacket(const ADARA::BeamlineInfoPkt &pkt);
  bool rxPacket(const ADARA::DeviceDescriptorPkt &pkt);
  bool rxPacket(const ADARA::VariableU32Pkt &pkt);
  bool rxPacket(const ADARA::VariableDoublePkt &pkt);
  bool rxPacket(const ADARA::VariableStringPkt &pkt);
  **********************/

  void displayPacketHeader( const ADARA::PacketHeader &hdr);  // helper for the above rxPacket() functions

  Ui::MainWindow *ui;

  int m_in;   // Posix file descriptor because that's what the packet parser expects for now

  QFile m_stdout; // for now, output to stdout.  In the future, this may become a QIODevice pointer
                  // that could point to a file (stdout) or maybe a TCP socket...

  const ADARA::Packet *m_pkt;  // holds onto the packet so we can send it out after looking at it on the screen

  // The following flags control the behavior of the packet parser
  bool m_displayPacket;  // Parse & display the packet on the screen
  bool m_singleStep; // Process one packet then stop

  // These two are used by rxPacket(const ADARA::Packet &pkt)
  // For now, I'm just leaving them false...
  bool m_hexDump;
  bool m_wordDump;

  char *m_printBuf;  // Used as a temporary space to store formatted text for the rxPacket() functions
  const unsigned m_printBufSize;


};

#endif // MAINWINDOW_H



