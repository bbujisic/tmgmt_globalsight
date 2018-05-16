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
 *   id = "globalsight",
 *   label = @Translation("GlobalSight translator"),
 *   description = @Translation("GlobalSight translator service."),
 *   ui = "Drupal\tmgmt_globalsight\GlobalSightTranslatorUi"
 * )
 */
class GlobalSightTranslator extends TranslatorPluginBase implements ContainerFactoryPluginInterface, ContinuousTranslatorInterface {

  /**
   * {@inheritdoc}
   */
  public static function create(ContainerInterface $container, array $configuration, $plugin_id, $plugin_definition) {
    return new static (
      $configuration,
      $plugin_id,
      $plugin_definition
    );
  }

  /**
   * {@inheritdoc}
   */
  public function getSupportedTargetLanguages(TranslatorInterface $translator, $source_language) {
    // @todo: Dependency injection!
    $gs = new TMGMTGlobalSightConnector($translator);
    $locales = $gs->getLocales();

    if (!($locales)) {
      return [];
    }

    // Forbid translations if source and target languages are not supported by GlobalSight.
    if (!in_array($source_language, $locales['source'])) {
      return [];
    }

    $target_languages = [];
    foreach ($locales['target'] as $target_language) {
      $target_languages[$target_language] = $target_language;
    }

    return $target_languages;
  }

  /**
   * {@inheritdoc}
   */
  public function requestTranslation(JobInterface $job) {
    $translator = $job->getTranslator();

    // @todo: Dependency injection!
    $gs = new TMGMTGlobalSightConnector($translator);

    // Send translation job to GlobalSight.
    if ($result = $gs->send($job, $translator->label(), $job->getRemoteTargetLanguage())) {
      // Okay we managed to send, but we are not sure if GS received translations. Check job status.
      $ok = $gs->uploadErrorHandler($result['jobName']);

      if ($ok) {
        // Make sure that there are not previous records of the job.
        _tmgmt_globalsight_delete_job($job->id());

        $record = array(
          'tjid' => $job->id(),
          'job_name' => $result ['jobName'],
          'status' => 1
        );
        $job->submitted('The translation job has been submitted.');
        \Drupal::database()->insert('tmgmt_globalsight')->fields($record)->execute();
      }
      else {
        // Cancel the job.
        $job->cancelled('Translation job was cancelled due to unrecoverable error.');
      }
    }
  }

  public function requestJobItemsTranslation(array $job_items) {
    // ContinuousTranslatorInterface
    // TODO: Implement requestJobItemsTranslation() method.
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
    $job_name = db_query('SELECT job_name FROM {tmgmt_globalsight} WHERE tjid = :tjid', array(
      ':tjid' => $job->id()
    ))->fetchField();

    $translator = $job->getTranslator();
    $gs = new TMGMTGlobalSightConnector ($translator);

    if ($status = $gs->cancel($job_name)) {
      _tmgmt_globalsight_archive_job($job->id());
      $job->aborted('The translation job has successfully been canceled');

      return TRUE;
    }

    return FALSE;
  }
}
