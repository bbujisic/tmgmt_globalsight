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
    // Initialize GlobalSight
    $gs = new TMGMTGlobalSightConnector($job->getTranslator());

      // Totally new translation job
      if ($result = $gs->send($job)) {
        $record = array(
          'tjid' => $job->tjid,
          'job_name' => $result['jobName'],
          'status' => 1,
        );
        $job->submitted('The translation job has been submitted.');
        drupal_write_record('tmgmt_cmswithtms', $record);
      }
  }

  /* what is this? */

  public function hasCheckoutSettings(TMGMTJob $job) {
    return TRUE;
  }

}
