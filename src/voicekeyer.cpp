/*
   voicekeyer.cpp
   
   Voice Keyer implementation
*/

#include "main.h"
#include "gui/dialogs/monitor_volume_adj.h"

extern std::atomic<SNDFILE*> g_sfRecMicFile;
std::atomic<bool>   g_recVoiceKeyerFile;
extern std::atomic<bool> g_voice_keyer_tx;
extern wxMutex g_mutexProtectingCallbackData;
extern std::mutex g_mutexProtectingRecordFiles;
extern std::atomic<bool> endingTx;
extern std::atomic<bool> g_monitorVoiceKeyerAudio;
extern std::atomic<float> g_monitorVoiceKeyerAudioVol;

void MainFrame::OnTogBtnVoiceKeyerClick (wxCommandEvent& event)
{
    // If recording a new VK file, stop doing that now.
    if (g_recVoiceKeyerFile.load(std::memory_order_relaxed))
    {       
        // The handle goes null under the mutex RecordStep's file thread
        // takes around its write; the file is closed once that's dropped,
        // so a write already under way can't land in a closed SNDFILE.
        SNDFILE* micRecordFile = nullptr;
        {
            std::unique_lock<std::mutex> lkRecordFiles(g_mutexProtectingRecordFiles);
            micRecordFile = g_sfRecMicFile.exchange(nullptr, std::memory_order_acq_rel);
        }

        g_mutexProtectingCallbackData.Lock();
        g_recVoiceKeyerFile.store(false, std::memory_order_relaxed);
        SetStatusText(wxT(""));
        g_mutexProtectingCallbackData.Unlock();

        if (micRecordFile != nullptr)
        {
            sf_close(micRecordFile);
        }
        
        // Cache the newly recorded file for playback.
        vkFileCache_.preload(vkFileName_);
        
        m_togBtnAnalog->Enable(true);
        m_togBtnVoiceKeyer->SetValue(false);
        m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
        
        // Switch back to previous tab once done with recording
        if (wxGetApp().appConfiguration.currentNotebookTab >= 0)
        {
            m_auiNbookCtrl->ChangeSelection(wxGetApp().appConfiguration.currentNotebookTab);
        }
    }
    else
    {
        if (vk_state == VK_IDLE)
        {
            // Check if VK file exists. If it doesn't, force the user to select another one.
            if (vkFileName_ == "" || !wxFile::Exists(vkFileName_))
            {
                vkFileName_ = "";
                wxCommandEvent tmpEvent;
                OnChooseAlternateVoiceKeyerFile(tmpEvent);
        
                if (vkFileName_ == "")
                {
                    // Cancel VK if user refuses to choose a new file.
                    m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
                    m_togBtnVoiceKeyer->SetValue(false);
                    goto end_handling;
                }
            }
            
            m_togBtnVoiceKeyer->SetValue(true);

            auto currentLabel = m_togBtnVoiceKeyer->GetLabel();
            currentLabel.Replace(_("Start Voice &Keyer"), _("Stop Voice &Keyer"), false);
            m_togBtnVoiceKeyer->SetLabel(currentLabel);

            VoiceKeyerProcessEvent(VK_START);

            // Nothing's being sent if the file turned out to be unusable.
            updateVoiceKeyerButtonLabel_();
        }
        else
            VoiceKeyerProcessEvent(VK_SPACE_BAR);
    }

end_handling:
    event.Skip();
}

void MainFrame::OnRecordNewVoiceKeyerFile( wxCommandEvent& )
{
    wxFileDialog saveFileDialog(
        this,
        _("Select Voice Keyer File"),
        wxGetApp().appConfiguration.voiceKeyerWaveFilePath,
        wxEmptyString,
        _("WAV files (*.wav)") + wxT("|*.wav|") +
        _("All files (*.*)") + wxT("|*.*"),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT
        );
        
    wxString soundFile;
    if(ShowPathDialog(saveFileDialog, soundFile) == wxID_CANCEL)
    {
        return;     // the user changed their mind...
    }
    
    // The below code ensures that the last folder the above dialog was
    // navigated to persists across executions.
    wxString tmpString = wxGetApp().appConfiguration.playFileToMicInPath;
    wxString fileName;
    wxString fileNameWithoutExt;
    wxString extension;
    wxFileName::SplitPath(soundFile, &tmpString, &fileNameWithoutExt, &extension);
    wxGetApp().appConfiguration.voiceKeyerWaveFilePath = tmpString;
    
    // Append .wav extension to the end if needed.
    if (extension.Lower() != wxT("wav"))
    {
        soundFile += ".wav";
    }
    fileName = wxFileName(soundFile).GetFullName();
    wxGetApp().appConfiguration.voiceKeyerWaveFile = fileName;
    
    int sample_rate = wxGetApp().appConfiguration.audioConfiguration.soundCard2In.sampleRate;
    SF_INFO     sfInfo;
    
    sfInfo.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    sfInfo.channels   = 1;
    sfInfo.samplerate = sample_rate;

    // A recording still going is finished off first: RecordStep's file thread
    // writes to whatever handle it's given, and a WAV's lengths are only
    // written when it's closed, so a take that's just replaced is left on disk
    // unreadable. It's detached under the mutex that thread takes around each
    // write, so nothing can be part way into it when it's closed. And it's
    // closed before the new file is opened, not after: the new one may be the
    // same file (the dialog offers the last one), which opening truncates,
    // and closing the old handle then would write its header and buffered
    // audio into the new recording.
    SNDFILE* previousFile;
    {
        std::unique_lock<std::mutex> lkRecordFiles(g_mutexProtectingRecordFiles);
        previousFile = g_sfRecMicFile.exchange(nullptr, std::memory_order_acq_rel);
    }
    if(previousFile != NULL) sf_close(previousFile);

    SNDFILE* newFile = sf_open(soundFile.c_str(), SFM_WRITE, &sfInfo);
    if(newFile == NULL)
    {
        wxString strErr = sf_strerror(NULL);
        wxMessageBox(strErr, _("Couldn't open sound file"), wxOK);
        return;
    }
    g_sfRecMicFile.store(newFile, std::memory_order_release);

    SetStatusText(wxString::Format(_("Recording file %s from microphone"), soundFile), 0);
    g_recVoiceKeyerFile.store(true, std::memory_order_relaxed);
    vkFileName_ = soundFile;
    
    // Switch tab to "From Mic" during recording.
    // Save currently visible plot so we can go back to it on completion.
    wxGetApp().appConfiguration.currentNotebookTab = captureCurrentMicGroupTab_();

    // Note: GetPageIndex sometimes returns the incorrect results, so iterating and finding
    // the current page ourselves is a better bet.
    size_t index = 0;
    for (; index < m_auiNbookCtrl->GetPageCount(); index++)
    {
        auto page = m_auiNbookCtrl->GetPage(index);
        if (page == (wxWindow *)m_panelSpeechIn)
        {
            m_auiNbookCtrl->ChangeSelection(index);
            page->Refresh();
            break;
        }
    }
    
    // Disable Analog and VK buttons while recording is happening
    m_togBtnAnalog->Enable(false);
    m_togBtnVoiceKeyer->SetValue(true);
    m_togBtnVoiceKeyer->SetBackgroundColour(*wxRED);
    
    m_togBtnVoiceKeyer->SetToolTip(wxString::Format(_("Toggle Voice Keyer using file %s. Right-click for additional options."), wxGetApp().appConfiguration.voiceKeyerWaveFile.get()));
    setVoiceKeyerButtonLabel_(fileNameWithoutExt);
}

void MainFrame::OnChooseAlternateVoiceKeyerFile( wxCommandEvent& )
{
    wxFileDialog openFileDialog(
        this,
        _("Select Voice Keyer File"),
        wxGetApp().appConfiguration.voiceKeyerWaveFilePath,
        wxEmptyString,
#if !defined(SNDFILE_NO_MP3_SUPPORT)
        _("Sound files (*.wav;*.mp3)") + wxT("|*.wav;*.mp3|") +
        _("WAV files (*.wav)") + wxT("|*.wav|") +
        _("MP3 files (*.mp3)") + wxT("|*.mp3|") +
#else
        _("WAV files (*.wav)") + wxT("|*.wav|") +
#endif // !defined(SNDFILE_NO_MP3_SUPPORT)
        _("All files (*.*)") + wxT("|*.*"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
        );

    wxString soundFile;
    if(ShowPathDialog(openFileDialog, soundFile) == wxID_CANCEL)
    {
        return;     // the user changed their mind...
    }

    // The below code ensures that the last folder the above dialog was
    // navigated to persists across executions.
    wxString tmpString = wxGetApp().appConfiguration.playFileToMicInPath;
    wxString fileName;
    wxString fileNameWithoutExt;
    wxString fileExt;
    wxFileName::SplitPath(soundFile, &tmpString, &fileNameWithoutExt, &fileExt);
    wxGetApp().appConfiguration.voiceKeyerWaveFilePath = tmpString;
    
    if (fileExt != "")
    {
        fileName = fileNameWithoutExt + "." + fileExt;
    }
    else
    {
        fileName = fileNameWithoutExt;
    }
    wxGetApp().appConfiguration.voiceKeyerWaveFile = fileName;
    
    vkFileName_ = soundFile;
    vkFileCache_.preload(vkFileName_);
    
    m_togBtnVoiceKeyer->SetToolTip(wxString::Format(_("Toggle Voice Keyer using file %s. Right-click for additional options."), wxGetApp().appConfiguration.voiceKeyerWaveFile.get()));
    setVoiceKeyerButtonLabel_(fileNameWithoutExt);
}

void MainFrame::OnTogBtnVoiceKeyerRightClick( wxContextMenuEvent& )
{
    // Only enable VK file selection on idle
    bool enabled = vk_state == VK_IDLE && !m_btnTogPTT->GetValue();
    chooseVKFileMenuItem_->Enable(vk_state == VK_IDLE);
    recordNewVoiceKeyerFileMenuItem_->Enable(enabled);

    // Trigger right-click menu popup in a location that will prevent it from
    // ending up off the screen.
    ShowPopupMenu(m_togBtnVoiceKeyer, voiceKeyerPopupMenu_, LeftOffsetContextMenuPosition(m_togBtnVoiceKeyer));
}

void MainFrame::OnSetMonitorVKAudio( wxCommandEvent& event )
{
    wxGetApp().appConfiguration.monitorVoiceKeyerAudio = event.IsChecked();
    g_monitorVoiceKeyerAudio.store(wxGetApp().appConfiguration.monitorVoiceKeyerAudio, std::memory_order_release);
    adjustMonitorVKVolMenuItem_->Enable(wxGetApp().appConfiguration.monitorVoiceKeyerAudio);
    
}

void MainFrame::OnSetMonitorVKAudioVol( wxCommandEvent& )
{
    auto popup = new MonitorVolumeAdjPopup(this, wxGetApp().appConfiguration.monitorVoiceKeyerAudioVol, g_monitorVoiceKeyerAudioVol);
    popup->Popup();
}

extern std::atomic<SNDFILE*> g_sfPlayFile;
extern std::unique_ptr<VoiceKeyerMemoryReader> g_sfPlayFileReader;
extern std::mutex g_mutexProtectingPlayFiles;
extern std::atomic<bool> g_playFileToMicIn;
extern std::atomic<bool> g_loopPlayFileToMicIn;
extern FreeDVInterface freedvInterface;
extern std::atomic<int> g_sfTxFs;

int MainFrame::VoiceKeyerStartTx(void)
{
    int next_state;

    // start playing wave file or die trying

    SF_INFO sfInfo;
    sfInfo.format = 0;

    // Prefer the in-memory copy so that we don't block on file I/O here (and
    // delay TX) if e.g. the file needs to be re-downloaded from cloud storage.
    std::unique_ptr<VoiceKeyerMemoryReader> tmpPlayFileReader;
    SNDFILE* tmpPlayFile = vkFileCache_.open(vkFileName_, &sfInfo, tmpPlayFileReader);
    if (tmpPlayFile == nullptr)
    {
        sfInfo.format = 0;
        tmpPlayFile = sf_open(vkFileName_.c_str(), SFM_READ, &sfInfo);
    }
    if(tmpPlayFile == NULL) {
        wxString strErr = sf_strerror(NULL);
        wxMessageBox(strErr, wxString::Format(_("Couldn't open: %s"), wxString::FromUTF8(vkFileName_.c_str())), wxOK);
        next_state = VK_IDLE;
        m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
        m_togBtnVoiceKeyer->SetValue(false);
    }
    else {
        g_sfTxFs.store(sfInfo.samplerate, std::memory_order_release);
        
        if (g_sfTxFs.load(std::memory_order_acquire) < freedvInterface.getRxSpeechSampleRate())
        {
            wxMessageBox(_("The selected voice keyer file does not have a high enough sample rate to guarantee acceptable audio quality. Please ensure that your file's sample rate is 16 kHz or greater."), _("Sample Rate Too Low"), wxOK);
            sf_close(tmpPlayFile);
            m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
            m_togBtnVoiceKeyer->SetValue(false);
            return VK_IDLE;
        }
       
        if (sfInfo.channels != 1)
        {
            wxMessageBox(_("The selected voice keyer file must only contain a single channel. Please use an audio editor to convert the file to a mono file."), _("Too Many Channels"), wxOK);
            sf_close(tmpPlayFile);
            m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
            m_togBtnVoiceKeyer->SetValue(false);
            return VK_IDLE;
        }
 
        {
            std::unique_lock<std::mutex> lk(g_mutexProtectingPlayFiles);

            // Close any file left over from a prior playback before replacing
            // its reader, as the reader must outlive the file.
            auto oldPlayFile = g_sfPlayFile.load(std::memory_order_acquire);
            if (oldPlayFile != nullptr)
            {
                sf_close(oldPlayFile);
            }

            g_sfPlayFile.store(tmpPlayFile, std::memory_order_release);
            g_sfPlayFileReader = std::move(tmpPlayFileReader);
        }
        
        SetStatusText(wxString::Format(_("Voice Keyer: Playing file %s to mic input"), wxString::FromUTF8(vkFileName_.c_str())), 0);
        g_loopPlayFileToMicIn.store(false, std::memory_order_relaxed);
        g_playFileToMicIn.store(true, std::memory_order_release);

        // Allow enabling VK during TX.
        if (!m_btnTogPTT->GetValue())
        {
            m_btnTogPTT->SetValue(true); togglePTT();
        }
        next_state = VK_TX;
        
        wxColour vkBackgroundColor(55, 155, 175);
        m_togBtnVoiceKeyer->SetBackgroundColour(vkBackgroundColor);

        if (wxGetApp().appConfiguration.monitorVoiceKeyerAudio)
        {
            g_voice_keyer_tx.store(true, std::memory_order_release);
        }
    }

    return next_state;
}

void MainFrame::updateVoiceKeyerButtonLabel_()
{
    if (!m_togBtnVoiceKeyer->GetValue())
    {
        auto currentLabel = m_togBtnVoiceKeyer->GetLabel();
        currentLabel.Replace(_("Stop Voice &Keyer"), _("Start Voice &Keyer"), false);
        m_togBtnVoiceKeyer->SetLabel(currentLabel);
    }
}

void MainFrame::VoiceKeyerProcessEvent(int vk_event) {
    int next_state = vk_state;
    
    switch(vk_state) {

    case VK_IDLE:
        g_voice_keyer_tx.store(false, std::memory_order_release);

        if (vk_event == VK_START) {
            // sample these puppies at start just in case they are changed while VK running
            vk_rx_pause = wxGetApp().appConfiguration.voiceKeyerRxPause;
            vk_repeats = wxGetApp().appConfiguration.voiceKeyerRepeats;
            log_debug("vk_rx_pause: %d vk_repeats: %d", vk_rx_pause, vk_repeats);

            vk_repeat_counter = 0;
            next_state = VoiceKeyerStartTx();
        }
        break;

     case VK_TX:

        // In this state we are transmitting and playing a wave file
        // to Mic In

        if (vk_event == VK_SPACE_BAR) {
            m_btnTogPTT->SetValue(false);
            m_btnTogPTT->SetBackgroundColour(wxNullColour);
#if !defined(__APPLE__)
            // macOS limitations prevent the foreground color of toggle buttons from being 
            // reliably set, so don't mess with it in the first place.
            m_btnTogPTT->SetForegroundColour(wxNullColour);
#endif // !defined(__APPLE__)
            endingTx.store(true, std::memory_order_release);
            togglePTT();
            m_togBtnVoiceKeyer->SetValue(false);
            m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
            updateVoiceKeyerButtonLabel_();
            next_state = VK_IDLE;
            CallAfter([&]() { StopPlayFileToMicIn(); });
        }

        if (vk_event == VK_PLAY_FINISHED) {
            m_btnTogPTT->SetValue(false);
            m_btnTogPTT->SetBackgroundColour(wxNullColour);
#if !defined(__APPLE__)
            // macOS limitations prevent the foreground color of toggle buttons from being 
            // reliably set, so don't mess with it in the first place.
            m_btnTogPTT->SetForegroundColour(wxNullColour);
#endif // !defined(__APPLE__)
            endingTx.store(true, std::memory_order_release);
            CallAfter([&]() { togglePTT(); });
            vk_repeat_counter++;
            if (vk_repeat_counter > vk_repeats) {
                m_togBtnVoiceKeyer->SetValue(false);
                m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
                updateVoiceKeyerButtonLabel_();
                next_state = VK_IDLE;
            }
            else {
                vk_rx_time = 0.0;
                next_state = VK_RX;
            }
        }

        break;

     case VK_RX:
        g_voice_keyer_tx.store(false, std::memory_order_release);

        // in this state we are receiving and waiting for
        // delay timer or valid sync

        if (vk_event == VK_DT) {
            if (freedvInterface.getSync() == 1) {
                // if we detect sync transition to SYNC_WAIT state
                next_state = VK_SYNC_WAIT;
                vk_rx_sync_time = 0.0;
            } else {
                vk_rx_time += DT;
                if (vk_rx_time >= vk_rx_pause) {
                    next_state = VoiceKeyerStartTx();
                }
            }
        }

        if (vk_event == VK_SPACE_BAR) {
            m_togBtnVoiceKeyer->SetValue(false);
            m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
            updateVoiceKeyerButtonLabel_();
            next_state = VK_IDLE;
        }

        break;

     case VK_SYNC_WAIT:
        g_voice_keyer_tx.store(false, std::memory_order_release);

        // In this state we wait for valid sync to last
        // VK_SYNC_WAIT_TIME seconds

        if (vk_event == VK_SPACE_BAR) {
            m_togBtnVoiceKeyer->SetValue(false);
            m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
            updateVoiceKeyerButtonLabel_();
            next_state = VK_IDLE;
        }

        if (vk_event == VK_DT) {
            if (freedvInterface.getSync() == 0) {
                // if we lose sync transition to RX State
                next_state = VK_RX;
            } else {
                vk_rx_time += DT;
                vk_rx_sync_time += DT;
            }

            // drop out of voice keyer if we get a few seconds of valid sync

            if (vk_rx_sync_time >= VK_SYNC_WAIT_TIME) {
                m_togBtnVoiceKeyer->SetValue(false);
                m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
                updateVoiceKeyerButtonLabel_();
                next_state = VK_IDLE;
            }
        }
        break;

    default:
        // catch anything we missed

        m_btnTogPTT->SetValue(false);
        m_btnTogPTT->SetBackgroundColour(wxNullColour);
#if !defined(__APPLE__)
        // macOS limitations prevent the foreground color of toggle buttons from being 
        // reliably set, so don't mess with it in the first place.
        m_btnTogPTT->SetForegroundColour(wxNullColour);
#endif // !defined(__APPLE__)
        endingTx.store(true, std::memory_order_release);
        togglePTT();
        m_togBtnVoiceKeyer->SetValue(false);
        m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
        updateVoiceKeyerButtonLabel_();
        next_state = VK_IDLE;
        g_voice_keyer_tx.store(false, std::memory_order_release);
    }
    
    vk_state = next_state;
}

