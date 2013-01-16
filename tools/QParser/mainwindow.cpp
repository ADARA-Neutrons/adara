#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <QString>
#include <QFileDialog>
#include <QDateTime>


MainWindow::MainWindow(QWidget *parent) :
  QMainWindow(parent),
  ADARA::Parser(),
  ui(new Ui::MainWindow),
  m_in( -1),
  m_pkt( NULL),
  m_displayPacket( true),
  m_singleStep( true),
  m_printBufSize(4096)  // 4K should be plenty
// Parser() will accept values for buffer size and max packet size, but the defaults are fine
{
  ui->setupUi(this);

  ui->textEdit->setWordWrapMode( QTextOption::NoWrap);
  ui->textEdit->setFontFamily( "courier");  // go for a mono-spaced font

  m_printBuf = new char[m_printBufSize];

  m_stdout.open( 1, QIODevice::WriteOnly);  // open up standard output
}

MainWindow::~MainWindow()
{
  if (m_in != -1)
  {
    ::close( m_in);
  }

  m_stdout.close();
  delete m_printBuf;
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
      ui->stepBtn->setEnabled(true);
      bool singleStepTemp = m_singleStep;
      m_singleStep = true;
      step();
      m_singleStep = singleStepTemp;
    }
    else
    {
      ui->stepBtn->setEnabled(false);
    }
  }
}

// Go to the beginning of the input file/stream, display the first packet
void MainWindow::resetInput()
{

  lseek( m_in, 0, SEEK_SET);  // Jump back to the beginning of the file

  // NOTE: Make sure the GUI properly reflects these changes!
  m_displayPacket = true;
  m_singleStep = true;

  step();
}

// A "step" consists of three parts:  sending the previous packet out via the output stream,
// reading the next packet in from the input stream and decoding & displaying it.  The
// first and last steps are optional (controlled by the boolean flags).  In practice, at
// least one is necessary for this to be at all useful...
void MainWindow::step()
{

  // If we're supposed to send out packets AND we have a packet to send out
  // then send it...
  if (ui->stdoutChk->isChecked() && m_pkt != NULL)
  {
      m_stdout.write( (const char *)m_pkt, m_pkt->packet_length());
      m_stdout.flush();
  }

  read( m_in);

  // the individual rxpacket() functions will handle the display

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


// RXPacket functions shamelessly ripped off of Dave's original adara-parser
// Minor changes made to output to QTextEdit instead of stdout with the
// expectation that more advanced work (multiple colors, for example)
// will happen as I have time...
bool MainWindow::rxPacket(const ADARA::Packet &pkt)
{
  m_pkt = &pkt;

  if (m_displayPacket)
  {
    ui->textEdit->clear();
    displayPacketHeader( pkt);

#if 0
    char buf[256];
    m_printBuf[0] = '\0';

    if (m_hexDump)
    {
      const uint8_t *p = pkt.packet();
      uint32_t addr;

      for (addr = 0; addr < pkt.packet_length(); addr++, p++)
      {
        if ((addr % 16) == 0)
        {
          sprintf(buf, "%04x:", addr);
          strcat( m_printBuf, buf);
        }
        sprintf(buf, " %02x", *p);
        strcat( m_printBuf, buf);

        if (addr % 16 == 15)
        {
          strcat( m_printBuf, "\n");
          ui->textEdit->append( m_printBuf);
        }
      }
      strcat( m_printBuf, "\n");
      ui->textEdit->append( m_printBuf);
    }

    if (m_wordDump)
    {
      const uint32_t *p = (const uint32_t *) pkt.packet();
      uint32_t addr;

      for (addr = 0; addr < pkt.packet_length(); addr += 4, p++)
      {
        sprintf( m_printBuf, "%04x: %08x\n", addr, *p);
        ui->textEdit->append( m_printBuf);
      }
    }
#endif
  }

  return true;

}


#if 0

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
