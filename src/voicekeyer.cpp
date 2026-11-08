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
        wxT("Select Voice Keyer File"),
        wxGetApp().appConfiguration.voiceKeyerWaveFilePath,
        wxEmptyString,
        wxT("WAV files (*.wav)|*.wav|")
        wxT("All files (*.*)|*.*"),
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT
        );
        
    if(saveFileDialog.ShowModal() == wxID_CANCEL)
    {
        return;     // the user changed their mind...
    }
    
    // The below code ensures that the last folder the above dialog was
    // navigated to persists across executions.
    wxString soundFile = saveFileDialog.GetPath();
    wxString tmpString = wxGetApp().appConfiguration.playFileToMicInPath;
    wxString fileName;
    wxString fileNameWithoutExt;
    wxString extension;
    wxFileName::SplitPath(soundFile, &tmpString, &fileNameWithoutExt, &extension);
    wxGetApp().appConfiguration.voiceKeyerWaveFilePath = tmpString;
    
    // Append .wav extension to the end if needed.
    if (extension.Lower() != _("wav"))
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

    g_sfRecMicFile.store(sf_open(soundFile.c_str(), SFM_WRITE, &sfInfo), std::memory_order_release);
    if(g_sfRecMicFile.load(std::memory_order_acquire) == NULL)
    {
        wxString strErr = sf_strerror(NULL);
        wxMessageBox(strErr, wxT("Couldn't open sound file"), wxOK);
        return;
    }

    SetStatusText(wxT("Recording file ") + soundFile + wxT(" from microphone") , 0);
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
    
    m_togBtnVoiceKeyer->SetToolTip(_("Toggle Voice Keyer using file ") + wxGetApp().appConfiguration.voiceKeyerWaveFile + _(". Right-click for additional options."));
    setVoiceKeyerButtonLabel_(fileNameWithoutExt);
}

void MainFrame::OnChooseAlternateVoiceKeyerFile( wxCommandEvent& )
{
    wxFileDialog openFileDialog(
        this,
        wxT("Select Voice Keyer File"),
        wxGetApp().appConfiguration.voiceKeyerWaveFilePath,
        wxEmptyString,
#if !defined(SNDFILE_NO_MP3_SUPPORT)
        wxT("Sound files (*.wav;*.mp3)|*.wav;*.mp3|")
        wxT("WAV files (*.wav)|*.wav|")
        wxT("MP3 files (*.mp3)|*.mp3|")
#else
        wxT("WAV files (*.wav)|*.wav|")
#endif // !defined(SNDFILE_NO_MP3_SUPPORT)
        wxT("All files (*.*)|*.*"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
        );

    if(openFileDialog.ShowModal() == wxID_CANCEL)
    {
        return;     // the user changed their mind...
    }

    // The below code ensures that the last folder the above dialog was
    // navigated to persists across executions.
    wxString tmpString = wxGetApp().appConfiguration.playFileToMicInPath;
    wxString soundFile = openFileDialog.GetPath();
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
    
    m_togBtnVoiceKeyer->SetToolTip(_("Toggle Voice Keyer using file ") + wxGetApp().appConfiguration.voiceKeyerWaveFile + _(". Right-click for additional options."));
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
    m_togBtnVoiceKeyer->PopupMenu(voiceKeyerPopupMenu_, LeftOffsetContextMenuPosition(m_togBtnVoiceKeyer));
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
        wxMessageBox(strErr, wxT("Couldn't open:") + wxString::FromUTF8(vkFileName_.c_str()), wxOK);
        next_state = VK_IDLE;
        m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
        m_togBtnVoiceKeyer->SetValue(false);
    }
    else {
        g_sfTxFs.store(sfInfo.samplerate, std::memory_order_release);
        
        if (g_sfTxFs.load(std::memory_order_acquire) < freedvInterface.getRxSpeechSampleRate())
        {
            wxMessageBox(wxT("The selected voice keyer file does not have a high enough sample rate to guarantee acceptable audio quality. Please ensure that your file's sample rate is 16 kHz or greater."), wxT("Sample Rate Too Low"), wxOK);
            sf_close(tmpPlayFile);
            m_togBtnVoiceKeyer->SetBackgroundColour(wxNullColour);
            m_togBtnVoiceKeyer->SetValue(false);
            return VK_IDLE;
        }
       
        if (sfInfo.channels != 1)
        {
            wxMessageBox(wxT("The selected voice keyer file must only contain a single channel. Please use an audio editor to convert the file to a mono file."), wxT("Too Many Channels"), wxOK);
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
        
        SetStatusText(wxT("Voice Keyer: Playing file ") + wxString::FromUTF8(vkFileName_.c_str()) + wxT(" to mic input") , 0);
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

