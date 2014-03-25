<?php

/**
 * @file
 * Please supply a file description.
 */

/**
 * @file
 * Provides Gengo translation plugin controller.
 */
class TMGMTGlobalSightTranslatorUIController extends TMGMTDefaultTranslatorUIController {

  /**
   * Overrides TMGMTDefaultTranslatorUIController::pluginSettingsForm().
   */
  public function pluginSettingsForm($form, &$form_state, TMGMTTranslator $translator, $busy = FALSE) {

    $form['endpoint'] = array(
      '#type' => 'textfield',
      '#title' => t('Webservice Endpoint URL'),
      '#default_value' => $translator->getSetting('endpoint'),
      '#description' => t('If you have not modified any configuration on your GlobalSight installation, this value should be http://globalsightip:port/globalsight/services/AmbassadorWebService'),
    );
    $form['username'] = array(
      '#type' => 'textfield',
      '#title' => t('GlobalSight username'),
      '#default_value' => $translator->getSetting('username'),
    );
    $form['password'] = array(
      '#type' => 'textfield',
      '#title' => t('GlobalSight password'),
      '#default_value' => $translator->getSetting('password'),
    );
    $form['proxyhost'] = array(
      '#type' => 'textfield',
      '#title' => t('Proxy host'),
      '#default_value' => $translator->getSetting('proxyhost'),
    );
    $form['proxyport'] = array(
      '#type' => 'textfield',
      '#title' => t('Proxy port'),
      '#default_value' => $translator->getSetting('proxyport'),
    );
    $form['file_profile_id'] = array(
      '#type' => 'textfield',
      '#title' => t('File profile ID'),
      '#default_value' => $translator->getSetting('file_profile_id'),
    );

    return parent::pluginSettingsForm($form, $form_state, $translator);
  }
  
  
//    public function checkoutSettingsForm($form, &$form_state, TMGMTJob $job) {
//    if ($translators = tmgmt_local_translators($job->source_language, array($job->target_language))) {
//      $form['translator'] = array(
//        '#title' => t('Select translator for this job'),
//        '#type' => 'select',
//        '#options' => array('' => t('Select user')) + $translators,
//        '#default_value' => $job->getSetting('translator'),
//      );
//    }
//    else {
//      $form['message'] = array(
//        '#markup' => t('There are no translators available.'),
//      );
//    }
//
//    return $form;
//  }

}
