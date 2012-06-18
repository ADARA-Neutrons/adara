// PVStreamerDlg.h : header file
//

#pragma once

#include <list>
#include <string>

#include "PVStreamer.h"
#include "ADARA_PVWriter.h"
#include "afxwin.h"

using namespace SNS::PVS;

// CPVStreamerDlg dialog
class CPVStreamerDlg : public CDialog, public SNS::PVS::IPVConfigListener, public SNS::PVS::IPVStreamListener, public SNS::PVS::ADARA::IADARAWriterListener
{
// Construction
public:
	CPVStreamerDlg(CWnd* pParent = NULL);	// standard constructor

    void                print( const std::string & a_msg );

// Dialog Data
	enum { IDD = IDD_PVSTREAMER_DIALOG };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


// Implementation
protected:
	HICON m_hIcon;


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

    void                OnCancel();

    std::string             m_log_text;
    std::list<std::string>  m_log_entries;
    unsigned long           m_start_time;

    // Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
//    afx_msg void OnBnClickedOk();
//    afx_msg void OnEnChangeLogEdit();
protected:
    CEdit m_log_edit;
public:
//    afx_msg void OnBnClickedOk();
    CEdit m_status_edit;
};
