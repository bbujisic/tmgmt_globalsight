<?php

/* +
 * @todo:
 *
 *   - Remove "Values" attribute from methods and add logic for it somewhere in
 *     object constructor
 */

class TMGMTGlobalSightConnector {

  // Settings
  public $base_url = '';
  private $username = '';
  private $password = '';
  private $endpoint = '';
  private $proxyhost = FALSE;  // ?
  private $proxyport = FALSE;  // ?
  private $file_profile_id = ''; // ?
  // Objects
  private $webservice;

  /**
   * Construct the connector to GlobalSight service.
   * Try to keep only this method specific to Drupal and TMGMT !!!!
   *
   * @param TMGMTTranslator $translator
   *   Translator which has the connection settings.
   */
  function __construct(TMGMTTranslator $translator) {
    $this->endpoint = $translator->getSetting('endpoint');
    $this->username = $translator->getSetting('username');
    $this->password = $translator->getSetting('password');
    $this->proxyhost = $translator->getSetting('proxyhost');
    $this->proxyport = $translator->getSetting('proxyport');
    $this->file_profile_id = $translator->getSetting('file_profile_id');
    $this->base_url = $GLOBALS['base_url'];

    module_load_include('php', 'tmgmt_globalsight', 'lib/nusoap/nusoap');
    //module_load_include('inc', 'tmgmt_globalsight', 'lib/GlobalSight.helpers');
    //module_load_include('inc', 'tmgmt_globalsight', 'menu/GlobalSight.dashboard');

    $this->webservice = new nusoap_client(GS_WSDL_URL, true);
    $this->webservice->setEndpoint($this->endpoint);
  }

  // Question: do we really have to login before each request (see get_locales), maybe we should cache access token...
  function login($values = array()) {
    if (isset($values['GlobalSight_proxyhost'])) {
      $this->proxyhost = $values['GlobalSight_proxyhost'];
    };

    if (isset($values['GlobalSight_proxyport'])) {
      $this->proxyhost = $values['GlobalSight_proxyport'];
    };

    $this->webservice->setHTTPProxy($this->proxyhost, $this->proxyport);

    $params = array('p_username' => $this->username, 'p_password' => $this->password);

    $result = $this->webservice->call('login', $params);

    if ($this->webservice->fault) {
      if (!($err = $this->webservice->getError())) {
        $err = 'No error details';
      }
      // I do not like watchdog here! Let's try and create an error handler class in any future refactor
      watchdog(
        'tmgmt_globalsight', "Error connecting to GlobalSight. Please make sure access credentials for !gs were properly set. <b>Error message:</b> %err", array(
        '!gs' => l('GlobalSight', 'admin/config/regional/tmgmt_translator/manage/GlobalSight'),
        '%err' => $err), WATCHDOG_ERROR);
      return FALSE;
    }

    return $result;
  }

  /**
   * Sends 'getFileProfileInfoEx' API request and parses a list of available
   * languages
   *
   * @param type $values
   * @return type
   */
  function getLocales() {
    $locales = array();

    if (!($access_token = $this->login())) {
      return FALSE;
    }

    $params = array('p_accessToken' => $access_token);
    $result = $this->webservice->call('getFileProfileInfoEx', $params);


    $profiles = simplexml_load_string($result);

    foreach ($profiles->fileProfile as $profile) {
      if ($profile->id == $this->file_profile_id) {
        $locales['source'][] = (string) $profile->localeInfo->sourceLocale;
        foreach ($profile->localeInfo->targetLocale as $locale) {
          $locales['target'][] = (string) $locale;
        }
      }
    }

    return $locales;
  }

  function generateJobTitle($job) {
    $hash = md5($this->base_url . $job->tjid . time());

    if ($job->source_language == "en") {
      // use post title + hash
      $post_title = str_replace(array(" ", "\t", "\n", "\r"), "_", $job->label);
      $post_title = preg_replace("/[^A-Za-z0-9_]/", "", $post_title);
      $post_title = substr($post_title, 0, 100) . '_' . $hash;
    }
    else {
      $post_title = 'dp_' . $hash;
    }

    return $post_title;
  }

  function encodeXML($job) {
    $strings = tmgmt_flatten_data($job->getData());

    $xml = "<?xml version='1.0' encoding='UTF-8' ?>";
    $xml .= "<fields id='$job->tjid'>";

    foreach ($strings as $key => $string) {
      if ($string['#translate']) {
        $xml .= "<field>";
        $xml .= "<name>$key</name>";
        $xml .= "<value><![CDATA[" . $string['#text'] . "]]></value>";
        $xml .= "</field>";
      }
    }

    $xml .= "</fields>";
    return $xml;
  }

  /**
   *  Function which encodes and sends translation job to GlobalSight
   *
   *  Major rewrite of gs_create_translation_job()
   */
  function send($job, $target_locale, $name = FALSE) {
    if (!($access_token = $this->login())) {
      return FALSE;
    }

    if (!$name) {
      $name = $this->generateJobTitle($job);
    }

    $xml = $this->encodeXML($job);

    $params = array(
      'accessToken' => $access_token,
      'jobName' => $name,
      'filePath' => 'GlobalSight.xml',
      'fileProfileId' => $this->file_profile_id,
      'content' => base64_encode($xml)
    );
    $response = $this->webservice->call('uploadFile', $params);

    $params = array(
      'accessToken' => $access_token,
      'jobName' => $name,
      'comment' => 'Drupal GlobalSight Translation Module',
      'filePaths' => 'GlobalSight.xml',
      'fileProfileIds' => $this->file_profile_id,
      'targetLocales' => $target_locale
    );
    $response = $this->webservice->call('createJob', $params);

    return $params;
  }

  function getStatus($job_name) {
    if (!($access_token = $this->login())) {
      return FALSE;
    }

    $params = array('p_accessToken' => $access_token, 'p_jobName' => $job_name);
    $result = $this->webservice->call('getStatus', $params);

    $xml = new SimpleXMLElement($result);
    return $this->xml2array($xml);
  }

  function receive($job_name) {

    if (!($access_token = $this->login())) {
      return FALSE;
    }

    $params = array('p_accessToken' => $access_token, 'p_jobName' => $job_name);
    $result = $this->webservice->call("getLocalizedDocuments", $params);
    $xml = new SimpleXMLElement($result);

    $download_url_prefix = $xml->urlPrefix;
    $target_locale = $xml->targetLocale;

    $result = $this->webservice->call("getJobExportFiles", $params);
    dsm($result);
    $xml = new SimpleXMLElement($result);
    $paths = $xml->paths;

    $results = array();
    foreach ($paths as $path) {
      $path = trim((string) $path);

      $aContext = array(
        'http' => array(
          'proxy' => $this->proxyhost . ":" . $this->proxyport,
          'request_fulluri' => true,
        ),
      );
      $cxContext = stream_context_create($aContext);

      if ($this->proxyhost && $this->proxyport) {
        $xml = file_get_contents($download_url_prefix . '/' . $path, False, $cxContext);
      }
      else {
        $xml = file_get_contents($download_url_prefix . '/' . $path);
      }

      dsm($xml);

      $xmlObject = simplexml_load_string($xml, 'SimpleXMLElement', LIBXML_NOCDATA);
      foreach ($xmlObject->field as $field) {
        $value = (string) $field->value;
        $key = (string) $field->name;
        $results[$key] = array(
          '#text' => $value,
        );
      }
    }

    return $results;
  }

  function code2status($code) {
    $a = array(
      0 => 'ARCHIVED', // internal status
      1 => 'DISPATCHED',
      2 => 'EXPORTED',
      3 => 'LOCALIZED',
      4 => 'CANCELED'
    );

    return $a[intval($code)];
  }

  /**
   * function xml2array
   *
   * This function is part of the PHP manual.
   *
   * The PHP manual text and comments are covered by the Creative Commons
   * Attribution 3.0 License, copyright (c) the PHP Documentation Group
   *
   * @author  k dot antczak at livedata dot pl
   * @date    2011-04-22 06:08 UTC
   * @link    http://www.php.net/manual/en/ref.simplexml.php#103617
   * @license http://www.php.net/license/index.php#doc-lic
   * @license http://creativecommons.org/licenses/by/3.0/
   * @license CC-BY-3.0 <http://spdx.org/licenses/CC-BY-3.0>
   */
  function xml2array($xmlObject, $out = array()) {
    foreach ((array) $xmlObject as $index => $node) {
      $out[$index] = ( is_object($node) ) ? $this->xml2array($node) : $node;
    }

    return $out;
  }

  function uploadErrorHandler($jobName) {
    $status = $this->getStatus($jobName);
    $status = $status['status'];

    // LEVERAGING appears to be normal status right after successfull upload
    if ($status == 'LEVERAGING') {
      return TRUE;
    }

    // IMPORT_FAILED appears to be status when XML file is corrupt
    if ($status == 'IMPORT_FAILED') {
      watchdog(
        'tmgmt_globalsight', "Error uploading file to GlobalSight. XML file appears to be corrupt or GlobalSight server timed out. Translation job canceled.", array(), WATCHDOG_ERROR);
      drupal_set_message(t('Error uploading file to GlobalSight. Translation job canceled.'), 'error');
      return FALSE;
    }

    if ($status == 'UPLOADING') {
      // Wait for 5 seconds and check status again
      sleep(5);
      $revised_status = $this->getStatus($jobName);
      $revised_status = $revised_status['status'];
      if ($revised_status == 'UPLOADING') {

        // Consolidate this messaging into an error handler and inject it as dependency
        watchdog(
          'tmgmt_globalsight', "Error creating job at GlobalSight. Translation job canceled.", array(), WATCHDOG_ERROR);
        drupal_set_message(t('Error creating job at GlobalSight. Translation job canceled.'), 'error');
        return FALSE;
      }
    }

    return TRUE;
  }

}
