<?php

/**
 * @file
 * Contains \Drupal\tmgmt_globalsight\Plugin\tmgmt\Translator\GlobalSightTranslator.
 */
namespace Drupal\tmgmt_globalsight\Plugin\tmgmt\Translator;

use Drupal\Core\Datetime\DrupalDateTime;
use Drupal\Core\Plugin\ContainerFactoryPluginInterface;
use Drupal\Core\Url;
use Drupal\tmgmt\ContinuousTranslatorInterface;
use Drupal\tmgmt\Entity\JobItem;
use Drupal\tmgmt\Entity\RemoteMapping;
use Drupal\tmgmt\Entity\Translator;
use Drupal\tmgmt\JobItemInterface;
use Drupal\tmgmt\SourcePreviewInterface;
use Drupal\tmgmt\TMGMTException;
use Drupal\tmgmt\TranslatorPluginBase;
use GuzzleHttp\Exception\RequestException;
use Symfony\Component\DependencyInjection\ContainerInterface;
use GuzzleHttp\ClientInterface;
use Drupal\tmgmt\TranslatorInterface;
use Drupal\tmgmt\JobInterface;
use Drupal\tmgmt\Translator\AvailableResult;

/**
 * GlobalSight translator plugin.
 *
 * @TranslatorPlugin(
 * id = "globalsight",
 * label = @Translation("GlobalSight translator"),
 * description = @Translation("GlobalSight translator service."),
 * ui = "Drupal\tmgmt_globalsight\GlobalSightTranslatorUi"
 * )
 */
class GlobalSightTranslator extends TranslatorPluginBase implements ContainerFactoryPluginInterface, ContinuousTranslatorInterface {
  
  /**
   *
   * {@inheritdoc}
   *
   */
  public function __construct(ClientInterface $client, array $configuration, $plugin_id, array $plugin_definition) {
    parent::__construct($configuration, $plugin_id, $plugin_definition);
    $this->client = $client;
  }

  /**
   * {@inheritdoc}
   */
  public static function create(ContainerInterface $container, array $configuration, $plugin_id, $plugin_definition) {
    $client = $container->get('http_client');
    return new static (
      
      $configuration,
      $plugin_id,
      $plugin_definition 
    );
  }

  /**
   *
   * {@inheritdoc}
   *
   */
  public function checkAvailable(TranslatorInterface $translator) {
    // Instantiate GlobalSightConnector object.
    $gs = new TMGMTGlobalSightConnector ( $translator );
    // Make sure we received proper list of locales from GlobalSight.
    if (! ($locales = $gs->getLocales ())) {
    return AvailableResult::no(t('@translator is not available. Make sure it is properly <a href=:configured>configured</a>.', [
      '@translator' => $translator->label(),
      ':configured' => $translator->url () 
    ]));
  }
    return AvailableResult::yes ();
  }
  
  /**
   * Overrides TMGMTDefaultTranslatorPluginController::checkTranslatable().
   */
  public function checkTranslatable(TranslatorInterface $translator, JobInterface $job) {
    return parent::checkTranslatable ( $translator, $job );
  }
  
  /**
   *
   * {@inheritdoc}
   *
   */
  public function requestTranslation(JobInterface $job) {
    $translator = $job->getTranslator ();
    // Instantiate GlobalSightConnector object.
    $gs = new TMGMTGlobalSightConnector ( $translator );
    
    // Send translation job to GlobalSight.
    if ($result = $gs->send ( $job, $translator->label (), $job->getRemoteTargetLanguage () )) {
      // Okay we managed to send, but we are not sure if GS received translations. Check job status.
      $ok = $gs->uploadErrorHandler ( $result ['jobName'] );
      
      if ($ok) {
        // Make sure that there are not previous records of the job.
        _tmgmt_globalsight_delete_job ( $job->id () );
        
        $record = array (
            'tjid' => $job->id (),
            'job_name' => $result ['jobName'],
            'status' => 1 
        );
        $job->submitted ( 'The translation job has been submitted.' );
        \Drupal::database ()->insert ( 'tmgmt_globalsight' )->fields ( $record )->execute ();
      } else {
        // Cancel the job.
        $job->cancelled ( 'Translation job was cancelled due to unrecoverable error.' );
      }
    }
  }
  
  /**
   *
   * {@inheritdoc}
   *
   */
  public function getGlobalSightTargetLanguages(TranslatorInterface $translator) {
    $gs = new TMGMTGlobalSightConnector ( $translator );
    $locales = $gs->getLocales ();
    $remote_languages = $locales ['target'];
    $ls = array ();
    
    foreach ( $remote_languages as $l ) {
      $ls [$l] = $l;
    }
    
    return $ls;
  }
  
  /**
   * Overrides TMGMTDefaultTranslatorPluginController::getSupportedTargetLanguages().
   */
  public function getSupportedTargetLanguages(TranslatorInterface $translator, $source_language) {
    $gs = new TMGMTGlobalSightConnector ( $translator );
    $locales = $gs->getLocales ();
    if (! ($locales)) {
      return array ();
    }
    
    // Forbid translations if source and target languages are not supported by GlobalSight.
    if (! in_array ( $source_language, $locales ['source'] )) {
      return array ();
    }
    
    $remote_languages = $locales ['target'];
    $ls = array ();
    
    foreach ( $remote_languages as $l ) {
      $ls [$l] = $l;
    }
    
    return $ls;
  }
  
  /**
   *
   * {@inheritdoc}
   *
   */
  public function hasCheckoutSettings(JobInterface $job) {
    return FALSE;
  }
  
  /**
   *
   * {@inheritdoc}
   *
   */
  public function abortTranslation(JobInterface $job) {
    $job_name = db_query ( 'SELECT job_name FROM {tmgmt_globalsight} WHERE tjid = :tjid', array (
        ':tjid' => $job->id() 
    ) )->fetchField ();
    
    $translator = $job->getTranslator ();
    $gs = new TMGMTGlobalSightConnector ( $translator );
    
    if ($status = $gs->cancel ( $job_name )) {
      _tmgmt_globalsight_archive_job ( $job->id() );
      $job->aborted ( 'The translation job has successfully been canceled' );
      return TRUE;
    }
    
    return FALSE;
  }
}
