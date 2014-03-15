<?php

class TMGMTCMSwithTMSTranslatorPluginController extends TMGMTDefaultTranslatorPluginController {

  public function canTranslate(TMGMTTranslator $translator, TMGMTJob $job) {

    if (!parent::canTranslate($translator, $job)) {
      return FALSE;
    }

    $gs = new TMGMTGlobalSightConnector($translator);

    if (!($locales = $gs->getLocales())) {
      return FALSE;
    }

    $maps = $this->getRemoteLanguagesMappings($translator);

    if (!in_array($maps[$job->source_language], $locales['source'])) {
      return FALSE;
    }

    if (!in_array($maps[$job->target_language], $locales['target'])) {
      return FALSE;
    }

    return TRUE;
  }

  public function requestTranslation(TMGMTJob $job) {
    $translator = $job->getTranslator();

    // Initialize GlobalSight
    $gs = new TMGMTGlobalSightConnector($translator);

    // Determine target language
    $maps = $this->getRemoteLanguagesMappings($translator);
    $target_locale = $maps[$job->target_language];

    // Totally new translation job
    if ($result = $gs->send($job, $target_locale)) {
      // Okay we managed to login, but we are not sure if GS received translations
      // Check job status
      $ok = $gs->uploadErrorHandler($result['jobName']);
      if ($ok) {
        $record = array(
          'tjid' => $job->tjid,
          'job_name' => $result['jobName'],
          'status' => 1,
        );
        $job->submitted('The translation job has been submitted.');
        drupal_write_record('tmgmt_cmswithtms', $record);
      }
      else {
        // cancel the job
        $job->cancelled('Translation job was cancelled due to unrecoverable error.');
      }
    }
  }

  /* what is this? */

  public function hasCheckoutSettings(TMGMTJob $job) {
    return TRUE;
  }

}
