#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <QString>
#include <QFileDialog>
#include <QDateTime>
#include <QByteArray>
#include <QStringList>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ADARA::Parser(),
  ui(new Ui::MainWindow),
  m_in( -1),
  m_pkt( NULL),
  m_displayPacket( true),
  m_keepReading( true)
// Parser() will accept values for buffer size and max packet size, but the defaults are fine
{
  ui->setupUi(this);

  ui->textEdit->setWordWrapMode( QTextOption::NoWrap);
  ui->textEdit->setFontFamily( "courier");  // go for a mono-spaced font

  // Add items to the combo box
  QStringList packetNames;
  packetNames << "Raw Data Packet"
    << "RTDL Packet"
    << "Source List Packet"
    << "Banked Event Packet"
    << "Beam Monitor Packet"
    << "Pixel Mapping Packet"
    << "Run Status Packet"
    << "Run Info Packet"
    << "Translation Complete Packet"
    << "Client Hello Packet"
    << "Annotation Packet"
    << "Sync Packet"
    << "Heartbeat Packet"
    << "Geometry Packet"
    << "Beamline Info Packet"
    << "Device Descriptor Packet"
    << "Variable U32 Packet"
    << "Variable Double Packet"
    << "Variable String Packet";
  ui->pktTypeCombo->insertItems(0, packetNames);
  ui->pktTypeCombo->setCurrentIndex( 0);

  // map the combo box indexes to the actual enum values (make sure this list
  // matches the order of the QStringList above!
  m_comboMap[0]  = ADARA::PacketType::RAW_EVENT_V0;
  m_comboMap[1]  = ADARA::PacketType::RTDL_V0;
  m_comboMap[2]  = ADARA::PacketType::SOURCE_LIST_V0;
  m_comboMap[3]  = ADARA::PacketType::BANKED_EVENT_V0;
  m_comboMap[4]  = ADARA::PacketType::BEAM_MONITOR_EVENT_V0;
  m_comboMap[5]  = ADARA::PacketType::PIXEL_MAPPING_V0;
  m_comboMap[6]  = ADARA::PacketType::RUN_STATUS_V0;
  m_comboMap[7]  = ADARA::PacketType::RUN_INFO_V0;
  m_comboMap[8]  = ADARA::PacketType::TRANS_COMPLETE_V0;
  m_comboMap[9]  = ADARA::PacketType::CLIENT_HELLO_V0;
  m_comboMap[10] = ADARA::PacketType::STREAM_ANNOTATION_V0;
  m_comboMap[11] = ADARA::PacketType::SYNC_V0;
  m_comboMap[12] = ADARA::PacketType::HEARTBEAT_V0;
  m_comboMap[13] = ADARA::PacketType::GEOMETRY_V0;
  m_comboMap[14] = ADARA::PacketType::BEAMLINE_INFO_V0;
  m_comboMap[15] = ADARA::PacketType::DEVICE_DESC_V0;
  m_comboMap[16] = ADARA::PacketType::VAR_VALUE_U32_V0;
  m_comboMap[17] = ADARA::PacketType::VAR_VALUE_DOUBLE_V0;
  m_comboMap[18] = ADARA::PacketType::VAR_VALUE_STRING_V0;

  m_stdout.open( 1, QIODevice::WriteOnly);  // open up standard output
}

MainWindow::~MainWindow()
{
  if (m_in != -1)
  {
    ::close( m_in);
  }

  m_stdout.close();
  delete m_pkt;
  delete ui;
}

// Pops up the file dialog, let's user pick the data file then opens it,
// storing the file descriptor in m_in
// Using posix file descriptors for now until Dave gets around to making a
// generic read function for the packet parser
void MainWindow::openFile()
{
  QString fileName = QFileDialog::getOpenFileName(this, tr("Open Data File"), ".", tr("ADARA Data Files (*.adara *.bin)"));
  if (fileName.size() > 0)
  {
    int temp_in = open( fileName.toAscii().constData(), O_RDONLY);
    if (temp_in != -1)
    {
      if (m_in != -1)
      {
        // close the existing file
        ::close(m_in);
      }

      m_in = temp_in;
    }

    if (m_in != -1)
    {
      // if we've got a file, then enable the single step button and
      // get the first packet
      ui->goBtn->setEnabled(true);
      ui->stepBtn->setEnabled(true);
      ui->resetBtn->setEnabled(true);
      reset();  // reset's the parser's internals (ie: we already had a file open and are switching to another one)
      step();
    }
    else
    {
      ui->goBtn->setEnabled(false);
      ui->stepBtn->setEnabled(false);
      ui->resetBtn->setEnabled(false);
    }
  }
}

// Go to the beginning of the input file/stream, display the first packet
void MainWindow::resetInput()
{
  lseek( m_in, 0, SEEK_SET);  // Jump back to the beginning of the file
  delete m_pkt;
  m_pkt = NULL;
  m_displayPacket = true;
  reset();  // reset's the parser's internals (ie: we already had a file open and are switching to another one)
  step();
}


// Start reading the file - when we stop depends on the various settings.
void MainWindow::go()
{
  m_keepReading = true;  // reset the bool in case it was false from the last call to go()
  m_displayPacket = false;  // No need to display packets while we're scanning through them
                            // (Wouldn't see them anyway since the GUI doesn't update while
                            // while we're in the loop below.)
  do
  {
    // Basically, we step until we get a stop condition...
    //  TODO: this should probably go into a background thread (or something that won't lock the GUI up...)
    step();

    if (ui->pktTypeRB->isChecked())
    {
      if (m_pkt->type() == m_comboMap.value( ui->pktTypeCombo->currentIndex()))
        // stop if the packet type radio button is checked AND the current packet's type matches the selected type
        m_keepReading = false;
    }
  } while ( m_keepReading);

  // Turn packet display back on then re-display the last packet
  m_displayPacket = true;
  displayPacket();
}


// A "step" consists of three parts:  sending the previous packet out via the output stream,
// reading the next packet in from the input stream and decoding & displaying it.  The
// first and last steps are optional (controlled by the boolean flags).  In practice, at
// least one is necessary for this to be at all useful...
void MainWindow::step()
{
  m_haveReadPacket = false;  // Used to test for EOF.  See below.

  // If we're supposed to send out packets AND we have a packet to send out
  // then send it...
  if (ui->stdoutChk->isChecked() && m_pkt != NULL)
  {
    m_stdout.write( (const char *)m_pkt->packet(), m_pkt->packet_length());
    m_stdout.flush();
  }

  read( m_in);  // will eventually call rxPacket(), which will copy the packet
                // into m_pkt

  if (m_displayPacket)
  {
    displayPacket();
  }

  // Check to see if we're at the end of the file.  If so, display a message and
  // disable the buttons
  // Note: this flag is really only a proxy for a true EOF test, and there's a variety
  // of ways to spoof it.  I'm not going to worry about that now.
  if (m_haveReadPacket == false)
  {
    QMessageBox msgBox;
    msgBox.setText("End of input file.");
    msgBox.exec();

    ui->goBtn->setEnabled(false);
    ui->stepBtn->setEnabled(false);

    m_keepReading = false;  // cause us to break out of the while loop in go() (if we were called from there)
  }
}


// Copy one packet out of the parser's buffer and into our own local memory (because the
// parser doesn't guarentee what will happen to the data once rxPacket() returns)
// Actual display and output is handled elsewhere
bool MainWindow::rxPacket( const ADARA::Packet &pkt)
{
  // Packet's copy constructor allocates memory and does a deep copy,
  // so the fact that we're calling new here for every packet probably
  // doesn't make things much slower....
  delete m_pkt;
  m_pkt = new ADARA::Packet( pkt);

  m_haveReadPacket = true;
  return true;
}

// Helper function for the other rxPacket() functions.  Displays info from the packet header
void MainWindow::displayPacketHeader( const ADARA::PacketHeader &hdr)
{

  // PulseID comes back as a 64 bit number, but we want to display it as 2 32-bit numbers
  unsigned pulseIDFirst = hdr.pulseId() >> 32;
  unsigned pulseIDSecond = (hdr.pulseId() & 0xFFFFFFFF);

  QDateTime timestamp;
  timestamp.setTime_t( hdr.timestamp().tv_sec);
  timestamp = timestamp.addMSecs( hdr.timestamp().tv_nsec / 1000000);

  unsigned pktType = hdr.type() >> 8;
  unsigned pktTypeVersion = hdr.type() & 0x000000FF;
  QString pktTypeStr;
  switch (pktType)
  {
  case 0x0000: pktTypeStr = "Raw Event Data"; break;
  case 0x0001: pktTypeStr = "Real Time Data Link Info"; break;
  case 0x0002: pktTypeStr = "Event Source List"; break;
  case 0x4000: pktTypeStr = "Banked Event Data"; break;
  case 0x4001: pktTypeStr = "Beam Monitor Event Data"; break;
  case 0x4002: pktTypeStr = "Pixel Mapping Table"; break;
  case 0x4003: pktTypeStr = "Run Status"; break;
  case 0x4004: pktTypeStr = "Run Information"; break;
  case 0x4005: pktTypeStr = "Translation Complete"; break;
  case 0x4006: pktTypeStr = "Client Hello"; break;
  case 0x4007: pktTypeStr = "Stream Annotation"; break;
  case 0x4008: pktTypeStr = "Synchronization (File)"; break;
  case 0x4009: pktTypeStr = "Heartbeat"; break;
  case 0x400A: pktTypeStr = "Geometry"; break;
  case 0x400B: pktTypeStr = "Beamline Info"; break;
  case 0x8000: pktTypeStr = "Device Descriptor"; break;
  case 0x8001: pktTypeStr = "Variable Value (u32)"; break;
  case 0x8002: pktTypeStr = "Variable Value (double)"; break;
  case 0x8003: pktTypeStr = "Variable Value (string)"; break;
  default:
    pktTypeStr = "UNKNOWN PACKET";
  }

  ui->textEdit->append( "Type:        \"" + pktTypeStr + "\" (version " + QString::number(pktTypeVersion) + ")");
  ui->textEdit->append( "Payload Len: " + QString::number(hdr.payload_length()));
  ui->textEdit->append( "Pulse ID:    " + QString::number(pulseIDFirst) + "." + QString::number(pulseIDSecond));
  ui->textEdit->append( "Packet Time: " + timestamp.toString( "MMM d, yyyy - hh:mm:ss.zzz"));
  ui->textEdit->append( "\n");
  // TODO: figure out how to put some kind of horizontal separator in here (instead of the blank line)
}

// Generic packet display (for times when we don't have display function specific
// to the packet type
void MainWindow::displayPacket()
{
  ui->textEdit->clear();
  displayPacketHeader( *m_pkt);

  // Dump the packet (including the header) in hex...
  QString oneLine;
  const uint8_t *p = m_pkt->packet();
  uint32_t addr = 0;

  while (addr < m_pkt->packet_length())
  {
    // one line of text consists of the address, 16 hex digits
    // followed 16 ASCII chars
    unsigned lineLength = 16;
    if (m_pkt->packet_length() - addr < 16)
    {
      lineLength = m_pkt->packet_length() - addr;
      // Properly handle the last line of the packet, which might
      // not have a full 16 bytes
    }

    oneLine = QString("0x%1 |").arg( addr, 4, 16, QChar('0'));
    // Note: has to be QChar('0').  Just using '0' does really strange things...

    for (unsigned i = 0; i < 16; i++)
    {
      if (i < lineLength)
      {
        oneLine.append( QString(" %1").arg( (unsigned)(*(p+i)), 2, 16, QChar('0')));
      }
      else
      {
        oneLine.append( " ..");  // filler to keep everything lined up nicely
      }
    }

    oneLine.append( " | ");

    QByteArray ba( (const char *)p, lineLength);
    // QTextEdit can gracefully handle the non-printable bytes, but it does actually
    // break on CR/LF chars, so we'll replace those with spaces
    for (int i=0; i < ba.size(); i++)
    {
      // using a switch statement to make it easier to add other substitions
      // in the future.
      switch (ba[i])
      {
      case 0x0A:
      case 0x0D:
        ba[i] = ' ';
        break;
      default:
        break; // do nothing
      }
    }

    oneLine.append( ba);

    ui->textEdit->append( oneLine);
    addr += lineLength;
    p += lineLength;
  }

  // Move the vertical scroll (if it was needed) back up to the top of the textEdit
  ui->textEdit->moveCursor (QTextCursor::Start) ;
  ui->textEdit->ensureCursorVisible() ;
}


#if 0
// RXPacket functions shamelessly ripped off of Dave's original adara-parser
// and modified to output to QTextEdit instead of stdout with the
// expectation that more advanced work (multiple colors, for example)
// will happen as I have time...

bool MainWindow::rxUnknownPkt(const ADARA::Packet &pkt)
{
        printf("%u.%09u Unknown Packet\n", (uint32_t) (pkt.pulseId() >> 32),
                (uint32_t) pkt.pulseId());
        printf("    type %08x len %u\n", pkt.type(), pkt.packet_length());

        return false;
}

bool MainWindow::rxOversizePkt(const ADARA::PacketHeader *hdr,
                           const uint8_t *chunk,
                           unsigned int chunk_offset,
                           unsigned int chunk_len)
{
        if (hdr) {
                printf("%u.%09u Oversize Packet\n",
                       (uint32_t) (hdr->pulseId() >> 32),
                       (uint32_t) hdr->pulseId());
                printf("    type %08x len %u\n", hdr->type(),
                       hdr->packet_length());
        }

        return false;
}

bool MainWindow::rxPacket(const ADARA::RawDataPkt &pkt)
{
        printf("%u.%09u RAW EVENT DATA\n"
               "    srcId 0x%08x pktSeq 0x%x dspSeq 0x%x%s\n"
               "    cycle %u%s veto 0x%x%s timing 0x%x flavor %d (%s)\n"
               "    intrapulse %luns tofOffset %luns%s\n"
               "    charge %lupC, %u events\n",
               (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
               pkt.sourceID(), pkt.pktSeq(), pkt.dspSeq(),
               pkt.endOfPulse() ? " EOP" : "",
               pkt.cycle(), pkt.badCycle() ? " (BAD)" : "",
               pkt.veto(), pkt.badVeto() ? " (BAD)" : "",
               pkt.timingStatus(), (int) pkt.flavor(),
               pulseFlavor(pkt.flavor()), (uint64_t) pkt.intraPulseTime() * 100,
               (uint64_t) pkt.tofOffset() * 100,
               pkt.tofCorrected() ? "" : " (raw)",
               (uint64_t) pkt.pulseCharge() * 10, pkt.num_events());

        if (1) {
                uint32_t len = pkt.payload_length();
                uint32_t *p = (uint32_t *) pkt.payload();
                uint32_t tof, i = 0;
                double s;

                /* Skip the header we handled above */
                p += 6;
                len -= 6 * sizeof(uint32_t);

                while (len) {
                        if (len < 8) {
                                fprintf(stderr, "Raw event packet too short\n");
                                return true;
                        }

                        /* Metadata TOF values have a cycle indicator in
                         * the upper 11 bits (really 10, but there is
                         * currently an unused bit at 31).
                         */
                        tof = p[0];
                        if (p[1] & 0x70000000)
                                tof &= ~0xffc00000;
                        s = 1e-9 * 100 * tof;
                        printf("\t  %u: %08x %08x    (%0.7f seconds)\n",
                                i++, p[0], p[1], s);
                        p += 2;
                        len -= 8;
                }
        }

        return false;
}

bool MainWindow::rxPacket(const ADARA::RTDLPkt &pkt)
{
        // TODO display FNA X fields
        printf("%u.%09u RTDL\n"
               "    cycle %u%s veto 0x%x%s timing 0x%x flavor %d (%s)\n"
               "    intrapulse %luns tofOffset %luns%s\n"
               "    charge %lupC period %ups\n",
               (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
               pkt.cycle(), pkt.badCycle() ? " (BAD)" : "",
               pkt.veto(), pkt.badVeto() ? " (BAD)" : "",
               pkt.timingStatus(), (int) pkt.flavor(),
               pulseFlavor(pkt.flavor()), (uint64_t) pkt.intraPulseTime() * 100,
               (uint64_t) pkt.tofOffset() * 100,
               pkt.tofCorrected() ? "" : " (raw)",
               (uint64_t) pkt.pulseCharge() * 10, pkt.ringPeriod());

        return false;
}

bool MainWindow::rxPacket(const ADARA::BankedEventPkt &pkt)
{
        printf("%u.%09u BANKED EVENT DATA\n"
               "    cycle %u charge %lupC energy %ueV\n",
               (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
               pkt.cycle(), (uint64_t) pkt.pulseCharge() * 10,
               pkt.pulseEnergy());
        if (pkt.flags()) {
                printf("    flags");
                if (pkt.flags() & ADARA::BankedEventPkt::ERROR_PIXELS)
                        printf(" ERROR");
                if (pkt.flags() & ADARA::BankedEventPkt::PARTIAL_DATA)
                        printf(" PARTIAL");
                if (pkt.flags() & ADARA::BankedEventPkt::PULSE_VETO)
                        printf(" VETO");
                if (pkt.flags() & ADARA::BankedEventPkt::MISSING_RTDL)
                        printf(" NO_RTDL");
                if (pkt.flags() & ADARA::BankedEventPkt::MAPPING_ERROR)
                        printf(" MAPPING");
                if (pkt.flags() & ADARA::BankedEventPkt::DUPLICATE_PULSE)
                        printf(" DUP_PULSE");
                printf("\n");
        }

        if (1) {
                uint32_t len = pkt.payload_length();
                uint32_t *p = (uint32_t *) pkt.payload();
                uint32_t nBanks, nEvents;

                /* Skip the header we handled above */
                p += 4;
                len -= 4 * sizeof(uint32_t);

                while (len) {
                        if (len < 16) {
                                fprintf(stderr, "Banked event packet too short "
                                                "(source section header)\n");
                                return true;
                        }

                        printf("    Source %08x intrapulse %luns "
                               "tofOffset %luns%s\n", p[0],
                               (uint64_t) p[1] * 100,
                               ((uint64_t) p[2] & 0x7fffffff) * 100,
                               (p[2] & 0x80000000) ? "" : " (raw)");
                        nBanks = p[3];
                        p += 4;
                        len -= 16;

                        for (uint32_t i = 0; i < nBanks; i++) {
                                if (len < 8) {
                                        fprintf(stderr, "Banked event packet "
                                                "too short (bank section "
                                                "header)\n");
                                        return true;
                                }

                                printf("\tBank 0x%x (%u events)\n", p[0], p[1]);
                                nEvents = p[1];
                                p += 2;
                                len -= 8;

                                if (len < (nEvents * 2 * sizeof(uint32_t))) {
                                        fprintf(stderr, "Banked event packet "
                                                "too short (events)\n");
                                        return true;
                                }

                                for (uint32_t j = 0; j < nEvents; j++) {
                                        printf("\t  %u: %08x %08x"
                                               "    (%0.7f seconds)\n",
                                               j, p[0], p[1],
                                               1e-9 * 100 * p[0]);
                                        p += 2;
                                        len -= 8;
                                }
                        }
                }
        }

        return false;
}

bool MainWindow::rxPacket(const ADARA::BeamMonitorPkt &pkt)
{
        printf("%u.%09u BEAM MONITOR DATA\n"
               "    cycle %u charge %lupC energy %ueV\n",
               (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
               pkt.cycle(), (uint64_t) pkt.pulseCharge() * 10,
               pkt.pulseEnergy());
        if (pkt.flags()) {
                printf("    flags");
                if (pkt.flags() & ADARA::BankedEventPkt::ERROR_PIXELS)
                        printf(" ERROR");
                if (pkt.flags() & ADARA::BankedEventPkt::PARTIAL_DATA)
                        printf(" PARTIAL");
                if (pkt.flags() & ADARA::BankedEventPkt::PULSE_VETO)
                        printf(" VETO");
                if (pkt.flags() & ADARA::BankedEventPkt::MISSING_RTDL)
                        printf(" NO_RTDL");
                if (pkt.flags() & ADARA::BankedEventPkt::MAPPING_ERROR)
                        printf(" MAPPING");
                if (pkt.flags() & ADARA::BankedEventPkt::DUPLICATE_PULSE)
                        printf(" DUP_PULSE");
                printf("\n");
        }

        if (1) {
                uint32_t len = pkt.payload_length();
                uint32_t *p = (uint32_t *) pkt.payload();
                uint32_t nEvents;

                /* Skip the header we handled above */
                p += 4;
                len -= 4 * sizeof(uint32_t);

                while (len) {
                        if (len < 12) {
                                fprintf(stderr, "Beam monitor event packet "
                                                "too short (monitor header)\n");
                                return true;
                        }

                        printf("    Monitor %u source %08x "
                               "tofOffset %luns%s\n", p[0] >> 22, p[1],
                               ((uint64_t) p[2] & 0x7fffffff) * 100,
                               (p[2] & 0x80000000) ? "" : " (raw)");
                        nEvents = p[0] & ((1 << 22) - 1);
                        p += 3;
                        len -= 12;

                        if (len < (nEvents * sizeof(uint32_t))) {
                                fprintf(stderr, "Beam monitor event packet "
                                                "too short (events)\n");
                                return true;
                        }

                        for (uint32_t i = 0; i < nEvents; p++, i++) {
                                printf("\t  %u: %0.7f seconds%s\n", i,
                                       1e-9 * 100 * (*p & ~(1U << 31)),
                                       (*p & (1U << 31)) ? " (trailing)" : "");
                        }

                        len -= nEvents * sizeof(uint32_t);
                }
        }

        return false;
}

bool MainWindow::rxPacket(const ADARA::PixelMappingPkt &pkt)
{
        // TODO display more fields (check that the table doesn't change)
        printf("%u.%09u PIXEL MAP TABLE\n", (uint32_t) (pkt.pulseId() >> 32),
               (uint32_t) pkt.pulseId());
        return false;
}

bool MainWindow::rxPacket(const ADARA::RunStatusPkt &pkt)
{
        printf("%u.%09u RUN STATUS\n",(uint32_t) (pkt.pulseId() >> 32),
               (uint32_t) pkt.pulseId());
        switch (pkt.status()) {
        case ADARA::RunStatus::NO_RUN:
                printf("    No current run\n");
                break;
        case ADARA::RunStatus::STATE:
                printf("    State snapshot\n");
                break;
        case ADARA::RunStatus::NEW_RUN:
                printf("    New run\n");
                break;
        case ADARA::RunStatus::RUN_EOF:
                printf("    End of file (run continues)\n");
                break;
        case ADARA::RunStatus::RUN_BOF:
                printf("    Beginning of file (continuing run)\n");
                break;
        case ADARA::RunStatus::END_RUN:
                printf("    End of run\n");
                break;
        }

        if (pkt.runNumber()) {
                printf("    Run %u started at epoch %u\n",
                       pkt.runNumber(), pkt.runStart());
                if (pkt.status() != ADARA::RunStatus::STATE)
                        printf("    File index %u\n", pkt.fileNumber());
        }

        return false;
}

bool MainWindow::rxPacket(const ADARA::RunInfoPkt &pkt)
{
        // TODO display more fields (check that the contents do not change)
        printf("%u.%09u RUN INFO\n", (uint32_t) (pkt.pulseId() >> 32),
               (uint32_t) pkt.pulseId());
        return false;
}

bool MainWindow::rxPacket(const ADARA::TransCompletePkt &pkt)
{
        printf("%u.%09u TRANSLATION COMPLETE\n",
               (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId());
        if (!pkt.status())
                printf("    Success");
        else if (pkt.status() < 0x8000)
                printf("    Transient failure");
        else
                printf("    Permament failure");
        if (pkt.reason().length())
                printf(", msg '%s'", pkt.reason().c_str());
        printf("\n");

        return false;
}

bool MainWindow::rxPacket(const ADARA::ClientHelloPkt &pkt)
{
        printf("%u.%09u CLIENT HELLO\n", (uint32_t) (pkt.pulseId() >> 32),
               (uint32_t) pkt.pulseId());
        if (pkt.requestedStartTime()) {
                if (pkt.requestedStartTime() == 1)
                        printf("    Request data from last run transition\n");
                else
                        printf("    Request data from timestamp %u\n",
                               pkt.requestedStartTime());
        } else
                printf("     Request data from current position\n");

        return false;
}

bool MainWindow::rxPacket(const ADARA::AnnotationPkt &pkt)
{
        printf("%u.%09u STREAM ANNOTATION\n", (uint32_t) (pkt.pulseId() >> 32),
               (uint32_t) pkt.pulseId());
        printf("    Type %u (%s%s)\n", pkt.type(), markerType(pkt.type()),
               pkt.resetHint() ? ", Reset Hint" : "");
        if (pkt.scanIndex())
                printf("    Scan Index %u\n", pkt.scanIndex());
        const std::string &comment = pkt.comment();
        if (comment.length())
                printf("    Comment '%s'\n", comment.c_str());

        return false;
}

bool MainWindow::rxPacket(const ADARA::SyncPkt &pkt)
{
        // TODO display more fields
        printf("%u.%09u SYNC\n", (uint32_t) (pkt.pulseId() >> 32),
               (uint32_t) pkt.pulseId());
        return false;
}

bool MainWindow::rxPacket(const ADARA::HeartbeatPkt &pkt)
{
        printf("%u.%09u HEARTBEAT\n", (uint32_t) (pkt.pulseId() >> 32),
               (uint32_t) pkt.pulseId());
        return false;
}

bool MainWindow::rxPacket(const ADARA::GeometryPkt &pkt)
{
        // TODO display more fields (check that the contents do not change)
        printf("%u.%09u GEOMETRY\n", (uint32_t) (pkt.pulseId() >> 32),
               (uint32_t) pkt.pulseId());
        return false;
}

bool MainWindow::rxPacket(const ADARA::BeamlineInfoPkt &pkt)
{
        printf("%u.%09u BEAMLINE INFO\n"
               "    id '%s' short '%s' long '%s'\n",
               (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
               pkt.id().c_str(), pkt.shortName().c_str(),
               pkt.longName().c_str());
        return false;
}

bool MainWindow::rxPacket(const ADARA::DeviceDescriptorPkt &pkt)
{
        // TODO display more fields (check that the contents don't change)
        printf("%u.%09u DEVICE DESCRIPTOR\n"
               "    Device %u\n",
               (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
               pkt.devId());
        printf("%s\n", pkt.description().c_str());
        return false;
}

bool MainWindow::rxPacket(const ADARA::VariableU32Pkt &pkt)
{
        printf("%u.%09u U32 VARIABLE\n"
               "    Device %u Variable %u\n"
               "    Status %s Severity %s\n"
               "    Value %u\n",
               (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
               pkt.devId(), pkt.varId(), statusString(pkt.status()),
               severityString(pkt.severity()), pkt.value());
        return false;
}

bool MainWindow::rxPacket(const ADARA::VariableDoublePkt &pkt)
{
        printf("%u.%09u DOUBLE VARIABLE\n"
               "    Device %u Variable %u\n"
               "    Status %s Severity %s\n"
               "    Value %f\n",
               (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
               pkt.devId(), pkt.varId(), statusString(pkt.status()),
               severityString(pkt.severity()), pkt.value());
        return false;
}

bool MainWindow::rxPacket(const ADARA::VariableStringPkt &pkt)
{
        printf("%u.%09u String VARIABLE\n"
               "    Device %u Variable %u\n"
               "    Status %s Severity %s\n"
               "    Value '%s'\n",
               (uint32_t) (pkt.pulseId() >> 32), (uint32_t) pkt.pulseId(),
               pkt.devId(), pkt.varId(), statusString(pkt.status()),
               severityString(pkt.severity()), pkt.value().c_str());
        return false;
}

#endif
