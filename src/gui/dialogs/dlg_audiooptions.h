//=========================================================================
// Name:          AudioInfoDisplay.h
// Purpose:       Declares simple wxWidgets application with GUI
//                created using wxFormBuilder.
// Authors:         David Rowe, David Witten
// 
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.1,
//  as published by the Free Software Foundation.  This program is
//  distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
//  License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//
//=========================================================================
#ifndef __AudioOptsDialog__
#define __AudioOptsDialog__

#include <thread>
#include <wx/listctrl.h>
#include <wx/combobox.h>
#include <wx/textctrl.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/dialog.h>
#include <wx/sizer.h>

#include "gui/controls/plot_scalar.h"

#define ID_AUDIO_OPTIONS    1000
#define AUDIO_IN            0
#define AUDIO_OUT           1

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// AudioInfoDisplay
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
class AudioInfoDisplay
{
    public:
        wxListCtrl*     m_listDevices;
        int             direction;
        wxTextCtrl*     m_textDevice;
        wxComboBox*     m_cbSampleRate;
};

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// class AudioOptsDialog
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
class AudioOptsDialog : public wxDialog
{
    private:
        std::thread*    m_audioPlotThread;
    protected:
        bool            m_isPaInitialized;

        void buildTestControls(PlotScalar **plotScalar, wxButton **btnTest, 
                               wxStaticBox *parentPanel, wxBoxSizer *bSizer, wxString const& buttonLabel);
        void plotDeviceInputForAFewSecs(wxString const& devName, wxString const& sampleRateText, PlotScalar *plotScalar);
        void plotDeviceOutputForAFewSecs(wxString const& devName, wxString const& sampleRateText, PlotScalar *plotScalar);

        int buildListOfSupportedSampleRates(wxComboBox *cbSampleRate, wxString const& devName, int in_out);
        void populateParams(AudioInfoDisplay);
        bool setTextCtrlIfDevNameValid(wxTextCtrl *textCtrl, wxListCtrl *listCtrl, wxString const& devName);
        void audioEngineInit(void);
        void OnDeviceSelect(wxComboBox *cbSampleRate, 
                            wxTextCtrl *textCtrl, 
                            wxListCtrl *listCtrlDevices, 
                            int         index,
                            int         in_out);

        AudioInfoDisplay m_RxInDevices;
        AudioInfoDisplay m_RxOutDevices;
        AudioInfoDisplay m_TxInDevices;
        AudioInfoDisplay m_TxOutDevices;
        wxPanel* m_panel1;
        wxNotebook* m_notebook1;

        wxPanel* m_panelRx;

        wxListCtrl* m_listCtrlRxInDevices;
        wxStaticText* m_staticText51;
        wxTextCtrl* m_textCtrlRxIn;
        wxStaticText* m_staticText6;
        wxComboBox* m_cbSampleRateRxIn;

        wxButton* m_btnRxInTest;
        PlotScalar* m_plotScalarRxIn;

        wxListCtrl* m_listCtrlRxOutDevices;
        wxStaticText* m_staticText9;
        wxTextCtrl* m_textCtrlRxOut;
        wxStaticText* m_staticText10;
        wxComboBox* m_cbSampleRateRxOut;

        wxButton* m_btnRxOutTest;
        PlotScalar* m_plotScalarRxOut;

        wxPanel* m_panelTx;

        wxListCtrl* m_listCtrlTxInDevices;
        wxStaticText* m_staticText12;
        wxTextCtrl* m_textCtrlTxIn;
        wxStaticText* m_staticText11;
        wxComboBox* m_cbSampleRateTxIn;

        wxButton* m_btnTxInTest;
        PlotScalar* m_plotScalarTxIn;

        wxListCtrl* m_listCtrlTxOutDevices;
        wxStaticText* m_staticText81;
        wxTextCtrl* m_textCtrlTxOut;
        wxStaticText* m_staticText71;
        wxComboBox* m_cbSampleRateTxOut;

        wxButton* m_btnTxOutTest;
        PlotScalar* m_plotScalarTxOut;

        wxButton* m_btnRefresh;
        wxStdDialogButtonSizer* m_sdbSizer1;
        wxButton* m_sdbSizer1OK;
        wxButton* m_sdbSizer1Apply;
        wxButton* m_sdbSizer1Cancel;
        
        // Virtual event handlers, override them in your derived class
        //virtual void OnActivateApp( wxActivateEvent& event ) { event.Skip(); }
//        virtual void OnCloseFrame( wxCloseEvent& event ) { event.Skip(); }

        void OnRxInDeviceSelect( wxListEvent& event );

        void OnRxInTest( wxCommandEvent& event );
        void OnRxOutTest( wxCommandEvent& event );
        void OnTxInTest( wxCommandEvent& event );
        void OnTxOutTest( wxCommandEvent& event );

        void OnRxOutDeviceSelect( wxListEvent& event );
        void OnTxInDeviceSelect( wxListEvent& event );
        void OnTxOutDeviceSelect( wxListEvent& event );
        void OnRefreshClick( wxCommandEvent& event );
        void OnApplyAudioParameters( wxCommandEvent& event );
        void OnCancelAudioParameters( wxCommandEvent& event );
        void OnOkAudioParameters( wxCommandEvent& event );
        // Virtual event handlers, override them in your derived class
        void OnClose( wxCloseEvent& event ) { event.Skip(); }
        void OnHibernate( wxActivateEvent& event ) { event.Skip(); }
        void OnIconize( wxIconizeEvent& event ) { event.Skip(); }
        void OnInitDialog( wxInitDialogEvent& event );

        void UpdatePlot(PlotScalar *plotScalar);
    public:

        AudioOptsDialog( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Audio Config"), const wxPoint& pos = wxPoint(1,1), const wxSize& size = wxDefaultSize, long style = wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER );
        ~AudioOptsDialog();
        int ExchangeData(int inout);
};
#endif //__AudioOptsDialog__
