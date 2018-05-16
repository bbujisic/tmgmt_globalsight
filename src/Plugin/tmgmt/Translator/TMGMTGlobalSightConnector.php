<?php

namespace Drupal\tmgmt_globalsight\Plugin\tmgmt\Translator;

use Drupal\tmgmt\JobInterface;
use Drupal\tmgmt\TranslatorInterface;
use Masterminds\HTML5\Exception;
use Symfony\Component\DependencyInjection\SimpleXMLElement;

/**
 * GlobalSight connector.
 */
class TMGMTGlobalSightConnector {
  public $base_url = '';
  private $username = '';
  private $password = '';
  private $endpoint = '';
  private $proxyhost = FALSE;
  private $proxyport = FALSE;
  private $file_profile_id = '';
  private $webservice;

  function __construct(TranslatorInterface $translator) {
    $this->endpoint = $translator->getSetting('endpoint');
    $this->username = $translator->getSetting('username');
    $this->password = $translator->getSetting('password');
    $this->file_profile_id = $translator->getSetting('file_profile_name');
    $this->base_url = $GLOBALS ['base_url'];

    ini_set('soap.wsdl_cache_enabled', 0);
    ini_set('soap.wsdl_cache_ttl', 0);

    $this->webservice = new \SoapClient($this->endpoint . '?wsdl', [
      'trace' => FALSE,
      'exceptions' => TRUE,
    ]);
    $this->webservice->__setLocation($this->endpoint);
  }

  /**
   * Authenticate with GS.
   *
   * @return string|null
   *    Token if login succeeded, otherwise logs error and returns NULL.
   */
  public function login() {
    return $this->call('login', [
      'p_username' => $this->username,
      'p_password' => $this->password,
    ]);
  }

  /**
   * getLocales method sends 'getFileProfileInfoEx' API request and parses a list of available languages.
   */
  function getLocales() {

    if (!($access_token = $this->login())) {
      return FALSE;
    }


    $fileProfiles = $this->getFileProfiles($access_token);
    if (isset($fileProfiles[$this->file_profile_id])) {
      $locales = [
        'source' => [$fileProfiles[$this->file_profile_id]['localeInfo']['sourceLocale']],
        'target' => $fileProfiles[$this->file_profile_id]['localeInfo']['targetLocale'],
      ];

      return $locales;
    }

    return FALSE;
  }

  /**
   * Method generates titles for GlobalSight by replacing unsupported characters with underlines and
   * adding some MD5 hash trails in order to assure uniqueness of job titles.
   *
   * @param JobInterface $job
   *            Loaded TMGMT Job object.
   * @return string GlobalSight job title.
   */
  function generateJobTitle($job, $label) {
    $hash = md5($this->base_url . $job->id() . time());
    if ($job->getSourceLangcode() == "en") {
      // use post title + hash
      $post_title = str_replace(array(
        " ",
        "\t",
        "\n",
        "\r"
      ), "_", $job->label());
      $post_title = preg_replace("/[^A-Za-z0-9_]/", "", $post_title);
      $post_title = substr($post_title, 0, 100) . '_' . $hash;
    }
    else {
      $post_title = 'dp_' . $hash;
    }

    return $post_title;
  }

  /**
   * Method generates XML document for GlobalSight based on TMGMTJob object.
   *
   * @param JobInterface $job
   *            Loaded TMGMT Job object.
   * @return string XML document as per GlobalSight API specifications.
   */
  function encodeXML($job) {
    $strings = \Drupal::service('tmgmt.data')->filterTranslatable($job->getData());
    $xml = "<?xml version='1.0' encoding='UTF-8' ?>";
    $xml .= "<fields id='" . $job->id() . "'>";
    foreach ($strings as $key => $string) {
      if ($string ['#translate']) {
        $xml .= "<field>";
        $xml .= "<name>$key</name>";
        $xml .= "<value><![CDATA[" . $string ['#text'] . "]]></value>";
        $xml .= "</field>";
      }
    }
    $xml .= "</fields>";

    return $xml;
  }

  public function getFileProfiles($access_token) {
    $result = $this->call('getFileProfileInfoEx', [
      'p_accessToken' => $access_token
    ]);

    $profiles = simplexml_load_string($result);

    $fpids = [];
    foreach ($profiles->fileProfile as $profile) {
      $fpids[(string) $profile->id] = $this->xml2array($profile);
    }

    return $fpids;
  }

  /**
   * Send method encodes and sends translation job to GlobalSight service.
   *
   * @param JobInterface $job
   *            Loaded TMGMT Job object.
   * @param string $label
   *            Job label.
   * @param string $target_locale
   *            locale code (e.g. en_US).
   * @param string $name
   *            job title.
   * @return array|false
   *            Array of parameters sent with CreateJob API call.
   */
  function send($job, $label, $target_locale, $name = FALSE) {
    if (!($access_token = $this->login())) {
      return FALSE;
    }

    $fileProfiles = $this->getFileProfiles($access_token);
    if (!isset($fileProfiles[$this->file_profile_id])) {
      // @todo: COME ON -- ERROR MESSAGES!
      return FALSE;
    }

    if (!$name) {
      $name = $this->generateJobTitle($job, $label);
    }

    $xml = $this->encodeXML($job);
    $params = array(
      'accessToken' => $access_token,
      'jobName' => $name,
      'filePath' => 'GlobalSight.xml',
      'fileProfileId' => $this->file_profile_id,
      'content' => base64_encode($xml)
    );
    $response = $this->call('uploadFile', $params);
    $params = array(
      'accessToken' => $access_token,
      'jobName' => $name,
      'comment' => 'Drupal GlobalSight Translation Module',
      'filePaths' => 'GlobalSight.xml',
      'fileProfileIds' => $this->file_profile_id,
      'targetLocales' => $target_locale
    );
    $response = $this->call('createJob', $params);

    return $params;
  }

  /**
   *
   * @param string $job_name
   *            GlobalSight job title.
   * @return mixed - FALSE : Ignore the status, move on...
   *         - "PERMANENT ERROR" : There is a permanent error at GS. Cancel the job.
   *         - API response converted to the array.
   */
  function getStatus($job_name) {
    if (!($access_token = $this->login())) {
      return FALSE;
    }

    $params = array(
      'p_accessToken' => $access_token,
      'p_jobName' => $job_name
    );
    $result = $this->call('getStatus', $params);

    if (!$result) {
      // I do not like watchdog here! Let's try and create an error handler class in any future refactor
      \Drupal::logger('tmgmt_globalsight')
        ->error("Error getting job status for !job_name. Translation job will be canceled. <br>", array(
          '!job_name' => $job_name,
        ));

      return 'PERMANENT ERROR';
    }

    try {
      $xml = new \SimpleXMLElement($result);

      return $this->xml2array($xml);
    }
    catch (Exception $err) {
      \Drupal::logger('tmgmt_globalsight')
        ->error("Error parsing XML for !job_name. Translation job will be canceled. <br> <b>Error message:</b><br> %err", array(
          '!job_name' => $job_name,
          '%err' => $err
        ));

      return 'PERMANENT ERROR';
    }
  }

  /**
   * Method cancel requests job deletion in GlobalSight.
   *
   * @param string $job_name
   *            GlobalSight job title.
   * @return mixed - FALSE: on any API error
   *         - API response in form of array
   */
  function cancel($job_name) {
    if (!($access_token = $this->login())) {
      return FALSE;
    }

    $params = array(
      'p_accessToken' => $access_token,
      'p_jobName' => $job_name
    );
    $result = $this->webservice->call('cancelJob', $params);

    if ($this->webservice->fault) {
      if (!($err = $this->webservice->getError())) {
        $err = 'No error details';
      }
      // I do not like watchdog here! Let's try and create an error handler class in any future refactor
      \Drupal::logger('tmgmt_globalsight')
        ->notice("Could not cancel !job_name job. <br> <b>Error message:</b><br> %err", array(
          '!job_name' => $job_name,
          '%err' => $err
        ));

      return FALSE;
    }

    $xml = new SimpleXMLElement ($result);

    return $this->xml2array($xml);
  }

  /**
   * This method downloads translations for a given GlobalSight job name.
   *
   * @param $job_name Title
   *            of the GlobalSight job
   * @return array|bool - FALSE: if API request failed due to any reason
   *         - API response in form of array
   */
  function receive($job_name) {
    if (!($access_token = $this->login())) {
      return FALSE;
    }

    $params = array(
      'p_accessToken' => $access_token,
      'p_jobName' => $job_name
    );
    $result = $this->webservice->call("getLocalizedDocuments", $params);
    $xml = new SimpleXMLElement ($result);
    $download_url_prefix = $xml->urlPrefix;
    $result = $this->webservice->call("getJobExportFiles", $params);
    $xml = new SimpleXMLElement ($result);
    $paths = $xml->paths;
    $results = array();
    $http_options = array();

    // Create stream context.
    // @todo: Test this...
    if ($this->proxyhost && $this->proxyport) {
      $aContext = array(
        'http' => array(
          'proxy' => $this->proxyhost . ":" . $this->proxyport,
          'request_fulluri' => TRUE
        )
      );
      $http_options ['context'] = stream_context_create($aContext);
    }

    foreach ($paths as $path) {
      $path = trim(( string ) $path);

      // $result = drupal_http_request($download_url_prefix . '/' . $path, $http_options);
      $data = file_get_contents($download_url_prefix . '/' . $path);
      $xmlObject = new SimpleXMLElement ($data);
      foreach ($xmlObject->field as $field) {
        $value = ( string ) $field->value;
        $key = ( string ) $field->name;
        $results [$key] ['#text'] = $value;
      }
    }

    return $results;
  }

  /**
   * Helper method translating GlobalSight status codes into integers.
   */
  function code2status($code) {
    $a = array(
      0 => 'ARCHIVED',
      1 => 'DISPATCHED',
      2 => 'EXPORTED',
      3 => 'LOCALIZED',
      4 => 'CANCELED'
    );

    return $a [intval($code)];
  }

  /**
   * Helper method recursively converting xml documents to array.
   */
  function xml2array($xmlObject, $out = array()) {
    foreach (( array ) $xmlObject as $index => $node) {
      $out [$index] = (is_object($node)) ? $this->xml2array($node) : $node;
    }

    return $out;
  }

  /**
   * Method checks if job upload to GlobalSight succeeded.
   *
   * @param $jobName Title
   *            of the GlobalSight job
   *
   * @return bool TRUE: if job import succeeded
   *         FALSE: if job import failed
   */
  function uploadErrorHandler($jobName) {
    $status = $this->getStatus($jobName);
    $status = $status['status'];

    // LEVERAGING appears to be normal status right after successful upload

    switch ($status) {

      case 'LEVERAGING' :
        return TRUE;
        break;

      // IMPORT_FAILED appears to be status when XML file is corrupt.
      case 'IMPORT_FAILED' :
        \Drupal::logger('tmgmt_globalsight')
          ->error("Error uploading file to GlobalSight. XML file appears to be corrupt or GlobalSight server timed out. Translation job canceled.", array());
        drupal_set_message(t('Error uploading file to GlobalSight. Translation job canceled.'), 'error');

        return FALSE;
        break;

      // UPLOADING can be normal message if translation upload did not finish, but, if unchanged for a period of time,
      // it can also be interpreted as "upload failed" message. So we need to have ugly time testing here.
      case 'UPLOADING' :
        // Wait for 5 seconds and check status again.
        sleep(5);
        $revised_status = $this->getStatus($jobName);
        $revised_status = $revised_status ['status'];
        if ($revised_status == 'UPLOADING') {

          // Consolidate this messaging into an error handler and inject it as dependency
          \Drupal::logger('tmgmt_globalsight')
            ->error("Error creating job at GlobalSight. Translation job canceled.", array());
          drupal_set_message(t('Error creating job at GlobalSight. Translation job canceled.'), 'error');

          return FALSE;
        }
        break;
    };

    return TRUE;
  }

  /**
   * Let this function run SOAP errands for us.
   *
   * @param $function_name
   * @param $params
   * @return null
   *
   * @todo: Document it a bit better.
   */
  private function call($function_name, $params) {
    try {
      $result = $this->webservice->__soapCall($function_name, $params);
    }
    catch (\SoapFault $sf) {
      \Drupal::logger('tmgmt_globalsight')
        ->notice("SOAP Fault code: !faultcode. <br> <b>Error message:</b><br> %err", array(
          '!faultcode' => $sf->getCode(),
          '%err' => $sf->getMessage(),
        ));

      return NULL;
    }

    return $result;
  }
}
