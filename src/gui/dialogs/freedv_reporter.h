//==========================================================================
// Name:            freedv_reporter.h
// Purpose:         Dialog that displays current FreeDV Reporter spots
// Created:         Jun 12, 2023
// Authors:         Mooneer Salem
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
//==========================================================================

#ifndef __FREEDV_REPORTER_DIALOG__
#define __FREEDV_REPORTER_DIALOG__

#include <string>
#include <map>
#include <deque>
#include <mutex>
#include <vector>

#include <wx/tipwin.h>
#include <wx/combo.h>
#include <wx/dataview.h>
#include <wx/regex.h>

class wxListBox;

#include "../../main.h"
#include "defines.h"
#include "filter_frequency.h"
#include "reporting/FreeDVReporter.h"
#include "../controls/ReportMessageRenderer.h"

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
// Class FreeDVReporterDialog
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=--=-=-=-=
class FreeDVReporterDialog : public wxFrame
{
    public:
        using FilterFrequency = ::FilterFrequency;

        enum ColumnFilterOperator
        {
            FILTER_GTE = 0,   // >=
            FILTER_GT  = 1,   // >
            FILTER_EQ  = 2,   // ==
            FILTER_NEQ = 3,   // !=
            FILTER_LT  = 4,   // <
            FILTER_LTE = 5,   // <=
            FILTER_NONE = -1, // no filter
        };
        
        FreeDVReporterDialog( wxWindow* parent,
                wxWindowID id = wxID_ANY, const wxString& title = _("FreeDV Reporter"), 
                const wxPoint& pos = wxDefaultPosition, 
                const wxSize& size = wxDefaultSize, 
                long style = wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU | wxMINIMIZE_BOX | wxRESIZE_BORDER);
        ~FreeDVReporterDialog();
        
        void setReporter(std::shared_ptr<FreeDVReporter> const& reporter);
        void refreshQSYButtonState();
        void refreshLayout();

        // Stops the window from live-tracking/persisting its own position via
        // OnMove. Callers should invoke this after deliberately grabbing and
        // saving the final position but before Close()/Destroy() -- closing
        // can trigger one last stray wxEVT_MOVE (observed reporting a
        // position with the title bar's height already stripped off) that
        // would otherwise silently overwrite the correct saved position.
        void stopTrackingPosition();
        
        void setBandFilter(FilterFrequency freq);

        static FilterFrequency getFilterForFrequency_(uint64_t freq);

        bool isTextMessageFieldInFocus();
        void ShowMsgItemContextMenu(int itemIdx);
    
        void Unselect(wxDataViewItem& dvi) { m_listSpots->Unselect(dvi); }
        
        bool getSelectedCallsignInfo(wxString& callsign, wxString& gridSquare, uint64_t& freqHz)
        {
            auto selectedItem = m_listSpots->GetSelection();
            if (selectedItem.IsOk())
            {
                FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
                return model->getSelectedCallsignInfo(selectedItem, callsign, gridSquare, freqHz);
            }
            return false;
        }
        
        bool getSelectedCallsignInfo(wxString& callsign)
        {
            auto selectedItem = m_listSpots->GetSelection();
            if (selectedItem.IsOk())
            {
                FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
                return model->getSelectedCallsignInfo(selectedItem, callsign);
            }
            return false;
        }

        wxString getGridSquareForCallsign(wxString const& callsign)
        {
            FreeDVReporterDataModel* model = (FreeDVReporterDataModel*)spotsDataModel_.get();
            return model->getGridSquareForCallsign(callsign);
        }
        
        void closeTooltip()
        {
            if (tipWindow_ != nullptr)
            {
                tipWindow_->Close();
                tipWindow_ = nullptr;
            }
        }
        
#if defined(WIN32)
        void autosizeColumns();
#endif // defined(WIN32)
            
    protected:

        // Handlers for events.
        void    OnOK(wxCommandEvent& event);
        void    OnSendQSY(wxCommandEvent& event);
        void    OnOpenWebsite(wxCommandEvent& event);
        void    OnClose(wxCloseEvent& event);
        void    OnInitDialog(wxInitDialogEvent& event);
        void    OnSize(wxSizeEvent& event);
        void    OnMove(wxMoveEvent& event);
        void    OnShow(wxShowEvent& event);
        void    OnBandFilterChange(wxCommandEvent& event);
        void    OnStatusTextSend(wxCommandEvent& event);
        void    OnStatusTextSendAndSaveMessage(wxCommandEvent& event);
        void    OnStatusTextSaveMessage(wxCommandEvent& event);
        void    OnStatusTextSendContextMenu(wxContextMenuEvent& event);
        void    OnStatusTextClear(wxCommandEvent& event);
        void    OnStatusTextClearContextMenu(wxContextMenuEvent& event);
        void    OnStatusTextClearSelected(wxCommandEvent& event);
        void    OnStatusTextClearAll(wxCommandEvent& event);
        void    OnStatusTextChange(wxCommandEvent& event);
        void    OnStatusMessageContextMenuEdit(wxCommandEvent& event);
        void    OnStatusMessageContextMenuDelete(wxCommandEvent& event);
        void    OnStatusMessageContextMenuAdd(wxCommandEvent& event);
        wxListBox* getMsgPopup() const;
        void    OnSystemColorChanged(wxSysColourChangedEvent& event);

        void OnItemSelectionChanged(wxDataViewEvent& event);
        void OnColumnClick(wxDataViewEvent& event);
        void OnItemDoubleClick(wxDataViewEvent& event);
        void OnItemRightClick(wxDataViewEvent& event);
        void OnColumnReordered(wxDataViewEvent& event);

        void OnTimer(wxTimerEvent& event);
        void DeselectItem();
        void DeselectItem(wxMouseEvent& event);
        void AdjustToolTip(wxMouseEvent& event);
        void OnFilterTrackingEnable(wxCommandEvent& event);
        void OnCopyUserMessage(wxCommandEvent& event);
        void SkipMouseEvent(wxMouseEvent& event);
        void AdjustMsgColWidth(wxListEvent& event);
        void OnRightClickSpotsList(wxContextMenuEvent& event);

        void OnShowColumn(wxCommandEvent& event);
        void OnIdleFilter(wxCommandEvent& event);
        void OnColumnHeaderRightClick(wxDataViewEvent& event);
        
        void OnQRZLookup(wxCommandEvent& event);
        void OnHamQTHLookup(wxCommandEvent& event);
        void OnHamCallLookup(wxCommandEvent& event);
        
        void OnLeftClickTooltip(wxMouseEvent& event);
        void OnMouseWheelTooltip(wxMouseEvent& event);
        void OnSetFocus(wxFocusEvent& event);
                
        // Main list box that shows spots
        wxDataViewCtrl*   m_listSpots;
        wxObjectDataPtr<wxDataViewModel> spotsDataModel_;
        wxMenu* spotsPopupMenu_;
        wxMenu* callsignPopupMenu_;
        wxString tempUserMessage_; // to store the currently hovering message prior to going on the clipboard
        wxString tempCallsign_; // to store the currently hovering callsign prior to lookup
        // QSY text
        wxTextCtrl* m_qsyText;
        
        // Band filter
        wxStaticText* m_filterStatus;
        wxComboBox* m_bandFilter;
        wxCheckBox* m_trackFrequency;
        wxRadioButton* m_trackFreqBand;
        wxRadioButton* m_trackExactFreq;

        // Status message
        wxComboCtrl* m_statusMessage;
        wxButton* m_buttonSend;
        wxButton* m_buttonClear;
        wxMenu* setPopupMenu_;
        wxMenuItem* setSaveMenuItem_;
        wxMenuItem* saveMenuItem_;
        wxMenu* clearPopupMenu_;
        wxMenu* msgItemPopupMenu_;
        wxMenuItem* addMsgMenuItem_;
        int contextMenuSelectedIndex_;
        
        // Step 4: test/save/cancel setup
        wxButton* m_buttonOK;
        wxButton* m_buttonSendQSY;
        wxButton* m_buttonDisplayWebpage;
        
        // Timer to unhighlight RX rows after 10s (like with web-based Reporter)
        wxTimer* m_resortTimer;
        wxTimer* m_highlightClearTimer;
        wxTimer* m_deleteTimer;

#if wxCHECK_VERSION(3,3,2)
        wxTipWindow::Ref tipWindow_;
#else
        wxTipWindow* tipWindow_;
#endif // wxCHECK_VERSION(3,3,2)
        
        // Menu bar and menu options
        wxMenuBar* menuBar_;
        wxMenu* showMenu_;
        wxMenu* filterMenu_;
        wxMenu* idleLongerThanMenu_;
        
     private:
        const wxString UNKNOWN_STR;
        const wxString MS_REMOVAL_RGX;
        const wxString TIMEZONE_RGX;
        const wxString TZ_OFFSET_STR;
        const wxString EMPTY_STR;
        const wxString TIME_FORMAT_STR;

         class FreeDVReporterDataModel : public wxDataViewModel
         {
         public:
             FreeDVReporterDataModel(FreeDVReporterDialog* parent);
             virtual ~FreeDVReporterDataModel();

             void setReporter(std::shared_ptr<FreeDVReporter> const& reporter);
             void setBandFilter(FilterFrequency freq);
             void refreshAllRows();
             void requestQSY(wxDataViewItem selectedItem, uint64_t frequency, wxString const& customText);
             void updateHighlights();
             void triggerResort();
             void requestColumnAutosize() { columnsNeedAutosize_ = true; }
             void deallocateRemovedItems();
             void updateMessage(wxString const& statusMsg)
             {
                 if (reporter_)
                 {
                     reporter_->updateMessage((const char*)statusMsg.utf8_str());
                 }
             }

             uint64_t getFrequency(wxDataViewItem item)
             {
                if (item.IsOk())
                {
                    auto data = (ReporterData*)item.GetID();
                    return data->frequency;
                }

                return 0;
             }

             FilterFrequency getCurrentBandFilter() const { return currentBandFilter_; }
             wxString getCallsign(wxDataViewItem& item)
             {
                 if (item.IsOk())
                 {
                     auto data = (ReporterData*)item.GetID();
                     return data->callsign;
                 }
                 return "";
             }
             
             wxString getRxCallsign(wxDataViewItem& item)
             {
                 if (item.IsOk())
                 {
                     auto data = (ReporterData*)item.GetID();
                     return data->lastRxCallsign;
                 }
                 return "";
             }
             
             wxString getUserMessage(wxDataViewItem& item)
             {
                 if (item.IsOk())
                 {
                     auto data = (ReporterData*)item.GetID();
                     return data->userMessage;
                 }
                 return "";
             }
             
             wxString getGridSquareForCallsign(wxString const& callsign)
             {
                 for (auto& kvp : allReporterData_)
                 {
                     auto reportData = kvp.second;
                     if (reportData->callsign == callsign)
                     {
                         return reportData->gridSquare;
                     }
                 }
                 
                 return "";
             }
             
             bool isValidForReporting()
             {
                 return reporter_ && reporter_->isValidForReporting();
             }
             
             bool getSelectedCallsignInfo(wxDataViewItem& item, wxString& callsign, wxString& gridSquare, uint64_t& freqHz)
             {
                 if (item.IsOk())
                 {
                     auto data = (ReporterData*)item.GetID();
                     callsign = data->callsign;
                     gridSquare = data->gridSquare;
                     freqHz = data->frequency;
                     return true;
                 }
                 return false;
             }

             bool filtersEnabled() const;

             void setColumnFilter(int col, int op, const wxString& val);
             void clearColumnFilter(int col);
             bool getColumnFilter(int col, int& op, wxString& val) const;
             bool hasAnyColumnFilter() const;
             
             bool getSelectedCallsignInfo(wxDataViewItem& item, wxString& callsign)
             {
                 if (item.IsOk())
                 {
                     auto data = (ReporterData*)item.GetID();
                     callsign = data->callsign;
                     return true;
                 }
                 return false;
             }
             
             // Required overrides to implement functionality
             virtual bool HasDefaultCompare() const override;
             virtual int Compare (const wxDataViewItem &item1, const wxDataViewItem &item2, unsigned int column, bool ascending) const override;
             virtual bool GetAttr (const wxDataViewItem &item, unsigned int col, wxDataViewItemAttr &attr) const override;
             virtual unsigned int GetChildren (const wxDataViewItem &item, wxDataViewItemArray &children) const override;
             virtual wxDataViewItem GetParent (const wxDataViewItem &item) const override;
             virtual void GetValue (wxVariant &variant, const wxDataViewItem &item, unsigned int col) const override;
             virtual bool IsContainer (const wxDataViewItem &item) const override;
             virtual bool SetValue (const wxVariant &variant, const wxDataViewItem &item, unsigned int col) override;

#if !wxCHECK_VERSION(3,2,0)
             virtual unsigned int GetColumnCount () const override;
             virtual wxString GetColumnType (unsigned int col) const override;
#endif // !wxCHECK_VERSION(3,2,0)

             bool sortOnNextTimerInterval;

         private:
             struct ReporterData
             {
                std::string sid;
                wxString callsign;
                wxString gridSquare;
                double distanceVal;
                wxString distance;
                double headingVal;
                wxString heading;
                wxString version;
                uint64_t frequency;
                wxString freqString;
                wxString status;
                wxString txMode;
                bool transmitting;
                wxString lastTx;
                wxDateTime lastTxDate;
                wxDateTime lastRxDate;
                wxString lastRxCallsign;
                wxString lastRxMode;
                wxString snr;
                int snrVal;
                wxString lastUpdate;
                wxDateTime lastUpdateDate;
                wxString userMessage;
                wxDateTime lastUpdateUserMessage;
                wxDateTime connectTime;

                // Controls whether this row has been filtered
                bool isVisible;
                
                // Controls whether this row is pending deletion
                bool isPendingDelete;
                wxDateTime deleteTime;

                // Controls whether this row is pending update
                bool isPendingUpdate;

                // Controls the current highlight color
                wxColour foregroundColor;
                wxColour backgroundColor;

                // Each column's text and its measured width (macOS and Windows; -1 if
                // not measured yet), as of the last check. cellTextsStale asks for the
                // text to be checked again, and only cells whose text has changed are
                // measured again.
                std::vector<wxString> cellTexts;
                std::vector<int> cellTextWidths;
                bool cellTextsStale = true;

                ReporterData()
                    : lastTxDate(wxInvalidDateTime)
                    , lastRxDate(wxInvalidDateTime)
                    , lastUpdateDate(wxInvalidDateTime)
                    , lastUpdateUserMessage(wxInvalidDateTime)
                    , connectTime(wxInvalidDateTime)
                    , deleteTime(wxInvalidDateTime)
                { /* empty */ }
            };

            struct CallbackHandler
            {
                std::string sid;
                std::string lastUpdate;
                std::string callsign;
                std::string gridSquare;
                std::string version;
                bool rxOnly;
                uint64_t frequencyHz;
                std::string txMode;
                bool transmitting;
                std::string lastTxDate;
                std::string receivedCallsign;
                float snr;
                std::string rxMode;
                std::string message;
                std::string connectTime;
                std::function<void(CallbackHandler&)> fn;
            };

            std::shared_ptr<FreeDVReporter> reporter_;
            std::map<std::string, ReporterData*> allReporterData_;
            std::deque<CallbackHandler> fnQueue_;
            std::mutex fnQueueMtx_;
            std::recursive_mutex dataMtx_;
            bool isConnected_;
            FreeDVReporterDialog* parent_;

            FilterFrequency currentBandFilter_;
            bool filterSelfMessageUpdates_;
            uint64_t filteredFrequency_;

            std::vector<int> columnFilterOperators_;
            std::vector<wxString> columnFilterValues_;

            bool isFiltered_(ReporterData* data);
            static bool isNumericColumn_(int col);
            wxString getColumnDisplayValue_(ReporterData* data, int col) const;

            void clearAllEntries_();

            void onReporterConnect_();
            void onReporterDisconnect_();
            void onUserConnectFn_(std::string sid, std::string lastUpdate, std::string callsign, std::string gridSquare, std::string version, bool rxOnly, std::string connectTime);
            void onUserDisconnectFn_(std::string sid, std::string const& lastUpdate, std::string const& callsign, std::string const& gridSquare, std::string const& version, bool rxOnly, std::string const& connectTime);
            void onFrequencyChangeFn_(std::string sid, std::string lastUpdate, std::string const& callsign, std::string const& gridSquare, uint64_t frequencyHz);
            void onTransmitUpdateFn_(std::string sid, std::string lastUpdate, std::string const& callsign, std::string const& gridSquare, std::string txMode, bool transmitting, std::string lastTxDate);
            void onReceiveUpdateFn_(std::string sid, std::string lastUpdate, std::string const& callsign, std::string const& gridSquare, std::string receivedCallsign, float snr, std::string rxMode);
            void onMessageUpdateFn_(std::string sid, std::string lastUpdate, std::string message);

            void onConnectionSuccessfulFn_();
            void onAboutToShowSelfFn_();
            
            void onRecvEndFn_();

            void execQueuedAction_();

            wxString makeValidTime_(std::string const& timeStr, wxDateTime& timeObj);

            void setColumnAutosize_(bool autosize);

            // Refitting columns to their contents measures every row, so on macOS and
            // Windows it's only done when the widest text in some column changes
            // (see maxTextWidths_). columnsNeedAutosize_ forces a refit regardless, e.g.
            // after header or unit changes; columnsNeedWidthCheck_ asks for a check after
            // rows are added/removed outside of updateHighlights().
#if defined(__APPLE__)
            bool columnsAutosized_;
#endif // defined(__APPLE__)
            bool columnsNeedAutosize_;
            bool columnsNeedWidthCheck_;

            // Widest text in each column at the last check. Most updates (e.g. a new
            // "last update" time) leave these unchanged and so need no refit.
            std::vector<int> maxTextWidths_;
            std::vector<bool> maxTextWidthChanged_; // per column, from the last check
            bool maxTextWidthsChanged_();
#if defined(__APPLE__)
            void refitChangedColumns_();
#endif // defined(__APPLE__)

            // Compiled once rather than for every timestamp; see makeValidTime_().
            wxRegEx millisecondsRgx_;
            wxRegEx timezoneRgx_;

            // Formats an SNR like wxNumberFormatter::ToString(snr, 1), without looking up
            // the locale's decimal separator every time.
            wxString formatSnr_(float snr) const;
            wxChar decimalSeparator_;
            
        };

        // Linux workaround to avoid inadvertent selections: set once the
        // pointer has been over a row.
        bool isSelectionPossible_ = false;

        void createColumn_(int col, bool visible);
        wxDataViewColumn* getColumnForModelColId_(unsigned int col);

        wxColour msgRowBackgroundColor;
        wxColour msgRowForegroundColor;
        wxColour txRowBackgroundColor;
        wxColour txRowForegroundColor;
        wxColour rxRowBackgroundColor;
        wxColour rxRowForegroundColor;

        void updateFilterStatus_();
        void saveColumnFilters_();
        void updateColumnFilterIndicators_();
};

#endif // __FREEDV_REPORTER_DIALOG__
