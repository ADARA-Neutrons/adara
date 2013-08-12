// PVStreamerDlg.h : header file
//

#pragma once

#include <list>
#include <string>

#include "PVStreamer.h"
#include "ADARA_PVWriter.h"
#include "afxwin.h"
#include <boost/thread.hpp>
#include "ComBusLite.h"

using namespace SNS::PVS;

// CPVStreamerDlg dialog
class CPVStreamerDlg : public CDialog, public SNS::PVS::IPVConfigListener, public SNS::PVS::IPVStreamListener, public SNS::PVS::ADARA::IADARAWriterListener, public SNS::PVS::IPVStreamerStatusListener
{
// Construction
public:
    CPVStreamerDlg( const std::string &a_topic_path, const std::string &a_broker_uri, unsigned short a_broker_port, const std::string &a_broker_user, const std::string &a_broker_pass );
    ~CPVStreamerDlg();

    void                print( const std::string & a_msg );

// Dialog Data
	enum { IDD = IDD_PVSTREAMER_DIALOG };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


// Implementation
protected:
	HICON m_hIcon;

    void                unhandledException( const TraceException &e );

    void                configurationLoaded( Protocol a_protocol, const std::string &a_source );
    void                configurationInvalid( Protocol a_protocol, const std::string &a_source );
    void                deviceActive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name );
    void                deviceInactive( Timestamp &a_time, Identifier a_dev_id, const std::string & a_name );
    void                pvActive( Timestamp &a_time, const PVInfo &a_pv_info );
    void                pvInactive( Timestamp &a_time, const PVInfo &a_pv_info );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, long a_value, const Enum *a_enum );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, unsigned long a_value );
    void                pvValueUpdated( Timestamp &a_time, const PVInfo &a_pv_info, double a_value );

    void                listening( const std::string & a_address, unsigned short a_port );
    void                connected( std::string &a_address );
    void                disconnected( std::string &a_address );

    std::string         timeString( Timestamp *ts ) const;
    void                addLogEntry( Timestamp *ts, std::string &entry );
    void                updateLogText();

    void                OnCancel();
    void                OnTimer( UINT a_timer_id );
    void                statusThread();

    std::string             m_log_text;
    std::list<std::string>  m_log_entries;
    unsigned long           m_start_time;
    boost::mutex            m_mutex;
    bool                    m_update_log;
    std::string             m_topic_path;
    std::string             m_broker_uri;
    unsigned short          m_broker_port;
    std::string             m_broker_user;
    std::string             m_broker_pass;
    ComBusLite             *m_combus;
    boost::thread          *m_status_thread;
    bool                    m_running;

    // Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()

protected:
    CEdit m_log_edit;
    CEdit m_status_edit;
    CEdit m_version_edit;
};
