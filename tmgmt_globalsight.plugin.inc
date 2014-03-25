<?php

class TMGMTGlobalSightTranslatorPluginController extends TMGMTDefaultTranslatorPluginController {

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

        // Make sure that there are not previous records of the job
        _tmgmt_globalsight_delete_job($job->tjid);

        $record = array(
          'tjid' => $job->tjid,
          'job_name' => $result['jobName'],
          'status' => 1,
        );
        $job->submitted('The translation job has been submitted.');
        drupal_write_record('tmgmt_globalsight', $record);
      }
      else {
        // cancel the job
        $job->cancelled('Translation job was cancelled due to unrecoverable error.');
      }
    }
  }

  public function cancelTranslation(TMGMTJob $job) {
    $job_name = db_query('SELECT job_name FROM {tmgmt_globalsight} WHERE tjid = :tjid', array(':tjid' => $job->tjid))->fetchField();

    $translator = $job->getTranslator();
    $gs = new TMGMTGlobalSightConnector($translator);

    if ($status = $gs->cancel($job_name)) {
      _tmgmt_globalsight_archive_job($job->tjid);
      $job->cancelled('The translation job has successfully been canceled');
      return TRUE;
    }
    
    return FALSE;
  }

  /* what is this? */

  public function hasCheckoutSettings(TMGMTJob $job) {
    return TRUE;
  }

}
