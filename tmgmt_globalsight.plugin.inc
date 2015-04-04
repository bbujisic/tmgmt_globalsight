<?php
/**
 * @file
 * Provides GlobalSight translator plugin controller.
 *
 * Check @link http://www.globalsight.com/
 * GlobalSight @endlink for more info.
 *
 */

/**
 * GlobalSight translator plugin controller.
 */
class TMGMTGlobalSightTranslatorPluginController extends TMGMTDefaultTranslatorPluginController {

  /**
   * Overrides TMGMTDefaultTranslatorPluginController::canTranslate().
   */
  public function canTranslate(TMGMTTranslator $translator, TMGMTJob $job) {

    // Use TMGMTDefaultTranslatorPluginController::canTranslate() validation.
    if (!parent::canTranslate($translator, $job)) {
      return FALSE;
    }

    // Instantiate GlobalSightConnector object.
    $gs = new TMGMTGlobalSightConnector($translator);

    // Make sure we received proper list of locales from GlobalSight.
    if (!($locales = $gs->getLocales())) {
      return FALSE;
    }

    // Get language mappings.
    $maps = $this->getRemoteLanguagesMappings($translator);

    // Forbid translations if source and target languages are not supported by GlobalSight.
    if (!in_array($maps[$job->source_language], $locales['source'])) {
      return FALSE;
    }
    if (!in_array($maps[$job->target_language], $locales['target'])) {
      return FALSE;
    }

    return TRUE;
  }

  /**
   * Overrides TMGMTTranslatorPluginControllerInterface::requestTranslation().
   */
  public function requestTranslation(TMGMTJob $job) {
    // Do not send children jobs to translation.
    if (isset($job->settings['child_job']) && $job->settings['child_job']) {
      return;
    }

    $translator = $job->getTranslator();

    // Instantiate GlobalSightConnector object.
    $gs = new TMGMTGlobalSightConnector($translator);

    // Determine target language.
    $maps = $this->getRemoteLanguagesMappings($translator);


    $target_locales[] = $maps[$job->target_language];

    // If translation will happen in bulk, add associated languages.
    if (isset($job->settings['associated_jobs'])) {
      foreach ($job->settings['associated_jobs'] as $language) {
        $target_locales[] = $maps[$language];
      }
    }


    // Send translation job to GlobalSight.
    if ($result = $gs->send($job, implode(',', $target_locales))) {
      // Okay we managed to send, but we are not sure if GS received translations. Check job status.
      $ok = $gs->uploadErrorHandler($result['jobName']);
      if ($ok) {

        // Make sure that there are not previous records of the job.
        _tmgmt_globalsight_delete_job($job->tjid);

        $record = array(
          'tjid' => $job->tjid,
          'job_name' => $result['jobName'],
          'status' => 1,
        );
        $job->submitted('The translation job has been submitted.');
        drupal_write_record('tmgmt_globalsight', $record);
      } else {
        // Cancel the job.
        $job->aborted('Translation job was aborted due to unrecoverable error.');
      }
    } else {
      // Cancel the job.
      $job->aborted('Translation job was aborted due to unrecoverable error.');
    }
  }

  /**
   * Overrides TMGMTDefaultTranslatorPluginController::cancelTranslation().
   */
  public function cancelTranslation(TMGMTJob $job) {

    $job_name = db_query('SELECT job_name FROM {tmgmt_globalsight} WHERE tjid = :tjid', array(':tjid' => $job->tjid))->fetchField();

    $translator = $job->getTranslator();
    $gs = new TMGMTGlobalSightConnector($translator);

    if ($status = $gs->cancel($job_name)) {
      // We're aborting the parent job.
      _tmgmt_globalsight_archive_job($job->tjid);
      $job->aborted('The translation job has successfully been canceled');

      // We need to abort kid jobs too.
      $kids = tmgmt_job_load_multiple(array_keys($job->settings['associated_jobs']));
      foreach ($kids as $childJob) {
        $childJob->aborted('The translation job has successfully been canceled');
      }

      return TRUE;
    }
    
    return FALSE;
  }

  /**
   * Overrides TMGMTDefaultTranslatorPluginController::hasCheckoutSettings().
   */
  public function hasCheckoutSettings(TMGMTJob $job) {
    return FALSE;
  }

}
