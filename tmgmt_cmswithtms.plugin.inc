<?php

class TMGMTCMSwithTMSTranslatorPluginController extends TMGMTDefaultTranslatorPluginController {

  public function canTranslate(TMGMTTranslator $translator, TMGMTJob $job) {

    if (!parent::canTranslate($translator, $job)) {
      return FALSE;
    }

    $gs = new TMGMTGlobalSightConnector($translator);

    $locales = $gs->getLocales();
    //$gs->receive();

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

    // try to load existing job (if any)
    $q = "SELECT * FROM  {tmgmt_cmswithtms} WHERE tjid = :tjid";
    $result = db_query($q, array(':tjid' => $job->tjid));
    $record = $result->fetchAssoc();

    if ($record) {
      // If job exists, and is not archived
      if ($record['status'] != 0) {
        // @TODO: Send API request to cancel previous translation job
      }
      // Create a new record
      if ($result = $gs->send($job)) {
        $record = array(
          'tjid' => $job->tjid,
          'job_name' => $result['jobName'],
          'status' => 1,
          'profile_id' => $result['fileProfileIds'],
        );
        $job->submitted('The translation job has been updated.');
        drupal_write_record('tmgmt_cmswithtms', $record, array('tjid'));
      }
      else {
        $job->rejected('Unknown error from Global Sight');
      }
    }
    else {
      // Totally new translation job
      if ($result = $gs->send($job)) {
        $record = array(
          'tjid' => $job->tjid,
          'job_name' => $result['jobName'],
          'status' => 1,
          'profile_id' => $result['fileProfileIds'],
        );
        $job->submitted('The translation job has been submitted.');
        drupal_write_record('tmgmt_cmswithtms', $record);
      }
    }
  }

  /* what is this? */

  public function hasCheckoutSettings(TMGMTJob $job) {
    return TRUE;
  }

}
