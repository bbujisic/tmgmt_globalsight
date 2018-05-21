<?php

namespace Drupal\tmgmt_globalsight;

use Drupal\tmgmt\JobInterface;
use Drupal\tmgmt\TranslatorInterface;
use Masterminds\HTML5\Exception;
use SoapFault;
use Symfony\Component\DependencyInjection\SimpleXMLElement;

/**
 * GlobalSight connector.
 */
class GlobalsightConnector {
  public $base_url = '';
  private $username = '';
  private $password = '';
  private $endpoint = '';
  private $file_profile_id = '';

  /** @var string */
  private $token = '';

  /** @var \SoapClient */
  private $webservice;

  public function init($endpoint, $username, $password, $file_profile_id) {
    $this->endpoint = $endpoint;
    $this->username = $username;
    $this->password = $password;
    $this->file_profile_id = $file_profile_id;
    $this->base_url = $GLOBALS ['base_url'];

    ini_set('soap.wsdl_cache_enabled', 0);
    ini_set('soap.wsdl_cache_ttl', 0);

    $this->webservice = new \SoapClient($this->endpoint . '?wsdl', [
      'trace' => FALSE,
      'exceptions' => TRUE,
    ]);
    $this->webservice->__setLocation($this->endpoint);

    $this->login();

    return $this->token;
  }

  /**
   * Authenticate with GS.
   */
  private function login() {
    // We don't want the code execution to continue if auth failed.
    // @todo: Figure out how to handle this better.
    $token = $this->call('login', [
      'p_username' => $this->username,
      'p_password' => $this->password,
    ]);

    if ($token instanceof \Exception) {
      $this->token = FALSE;
    }
    else {
      $this->token = $token;
    }
  }

  /**
   * getLocales method sends 'getFileProfileInfoEx' API request and parses a list of available languages.
   */
  function getLocales() {
    $fileProfiles = $this->getFileProfiles();
    if (isset($fileProfiles[$this->file_profile_id])) {
      $locales = [
        'source' => [$fileProfiles[$this->file_profile_id]['localeInfo']['sourceLocale']],
        'target' => $fileProfiles[$this->file_profile_id]['localeInfo']['targetLocale'],
      ];

      return $locales;
    }

    return FALSE;
  }


  public function getFileProfiles() {
    $result = $this->call('getFileProfileInfoEx', [
      'p_accessToken' => $this->token
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
   * Essentially, it runs 3 subsequent API methods in order to upload files
   * and check whether the upload succeeded:
   *   - uploadFile
   *   - createJob
   *   - getStatus
   *
   * @param int $jobId
   *   An ID of the job.
   *   @todo: Consider removing $jobId, it may be useless.
   * @param string $label
   *   Job label.
   * @param string $target_locale
   *   locale code (e.g. en_US).
   * @param array $translation_strings
   *   A key/value array of strings to be translated.
   *
   * @return FALSE|string
   *   Job title (which is an amended job label). FALSE if the upload failed.
   */
  public function send($jobId, $label, $target_locale, $translation_strings) {

    $fileProfiles = $this->getFileProfiles();
    if (!isset($fileProfiles[$this->file_profile_id])) {
      // @todo: COME ON -- ERROR MESSAGES!
      return FALSE;
    }

    $name = $this->generateJobTitle($jobId, $label);
    $xml = $this->prepareXML($jobId, $translation_strings);

    $response = $this->call('uploadFile', [
      'accessToken' => $this->token,
      'jobName' => $name,
      'filePath' => 'GlobalSight.xml',
      'fileProfileId' => $this->file_profile_id,
      'content' => $xml
    ]);
    if ($response instanceof \Exception) {
      return FALSE;
    }

    // So file was "probably" successfully uploaded, we cannot really
    // know as GlobalSight's uploadFile function is void!

    $response = $this->call('createJob', [
      'accessToken' => $this->token,
      'jobName' => $name,
      'comment' => 'Drupal GlobalSight Translation Module',
      'filePaths' => 'GlobalSight.xml',
      'fileProfileIds' => $this->file_profile_id,
      'targetLocales' => $target_locale
    ]);
    if ($response instanceof \Exception) {
      return FALSE;
    }

    // Finally, the only way to really tell if the job upload succeeded
    // is the getStatus method.
    if ($this->getStatus($name)) {
      return $name;
    };

    return FALSE;
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

    $result = $this->call('getStatus', [
      'p_accessToken' => $this->token,
      'p_jobName' => $job_name
    ]);

    if (!$result) {
      // I do not like watchdog here! Let's try and create an error handler class in any future refactor
      \Drupal::logger('tmgmt_globalsight')
        ->error("Error getting job status for %job_name. Translation job will be canceled. <br>", array(
          '%job_name' => $job_name,
        ));

      return 'PERMANENT ERROR';
    }

    try {
      $xml = new \SimpleXMLElement($result);

      return $this->xml2array($xml);
    }
    catch (Exception $err) {
      \Drupal::logger('tmgmt_globalsight')
        ->error("Error parsing XML for %job_name. Translation job will be canceled. <br> <b>Error message:</b><br> %err", array(
          '%job_name' => $job_name,
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

    $result = $this->call('cancelJob', [
      'p_accessToken' => $this->token,
      'p_jobName' => $job_name
    ]);

    if ($result instanceof Exception) {
      return FALSE;
    }

    $xml = new \SimpleXMLElement($result);

    // @todo: will this work?
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
    $params = array(
      'p_accessToken' => $this->token,
      'p_jobName' => $job_name
    );
    $result = $this->webservice->call("getLocalizedDocuments", $params);
    $xml = new \SimpleXMLElement($result);
    $download_url_prefix = $xml->urlPrefix;
    $result = $this->webservice->call("getJobExportFiles", $params);
    $xml = new \SimpleXMLElement($result);
    $paths = $xml->paths;
    $results = array();

    foreach ($paths as $path) {
      $path = trim((string) $path);

      // $result = drupal_http_request($download_url_prefix . '/' . $path, $http_options);
      $data = file_get_contents($download_url_prefix . '/' . $path);
      $xmlObject = new \SimpleXMLElement($data);
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
  public function uploadErrorHandler($jobName) {
    $status = $this->getStatus($jobName);
    $status = $status['status'];

    // LEVERAGING appears to be normal status right after successful upload

    switch ($status) {

      case 'LEVERAGING':
        return TRUE;
        break;

      // IMPORT_FAILED appears to be status when XML file is corrupt.
      case 'IMPORT_FAILED':
        \Drupal::logger('tmgmt_globalsight')
          ->error("Error uploading file to GlobalSight. XML file appears to be corrupt or GlobalSight server timed out. Translation job canceled.", array());
        drupal_set_message(t('Error uploading file to GlobalSight. Translation job canceled.'), 'error');

        return FALSE;
        break;

      // UPLOADING can be normal message if translation upload did not finish, but, if unchanged for a period of time,
      // it can also be interpreted as "upload failed" message. So we need to have ugly time testing here.
      case 'UPLOADING':
        // Wait for 5 seconds and check status again.
        sleep(5);
        $revised_status = $this->getStatus($jobName);
        $revised_status = $revised_status['status'];
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
   * @return string|Exception
   *
   * @todo: Document it a bit better.
   */
  public function call($function_name, $params = []) {
    try {
      $result = $this->webservice->__soapCall($function_name, $params);
    }
    catch (\SoapFault $sf) {
      \Drupal::logger('tmgmt_globalsight')
        ->notice("<b>Function call: </b>%func<br>
                    <b>Parameters:</b><br><pre>%params</pre><br>
                    <b>SOAP Fault code:</b> %faultcode.<br>
                    <b>Error message:</b><br>%err", [
          '%func' => $function_name,
          '%params' => json_encode($params),
          '%faultcode' => $sf->getCode(),
          '%err' => $sf->getMessage(),
        ]);

      return $sf;
    }

    return $result;
  }

  /**
   * Prepare XML document to be sent to GlobalSight.
   *
   * @param string $job_id
   *   Pretty self-explanatory.
   * @param array $translation_strings
   *   Key/value array of strings to be translated.
   * @return string
   *   XML document to be fed to GlobalSight.
   */
  private function prepareXML($job_id, $translation_strings) {
    $xml = "<?xml version='1.0' encoding='UTF-8' ?>";
    $xml .= "<fields id='" . $job_id . "'>";
    foreach ($translation_strings as $key => $value) {
      $xml .= "<field>";
      $xml .= "<name>" . $key . "</name>";
      $xml .= "<value><![CDATA[" . $value . "]]></value>";
      $xml .= "</field>";
    };
    $xml .= "</fields>";

    return $xml;
  }

  /**
   * Method generates titles for GlobalSight by replacing unsupported characters with underlines and
   * adding some MD5 hash trails in order to assure uniqueness of job titles.
   *
   * @param JobInterface $job
   *            Loaded TMGMT Job object.
   * @return string GlobalSight job title.
   * @todo: Fix PHPDoc!
   * @todo: Could we use an internal GS method for this, even at the cost of an additional request?
   */
  private function generateJobTitle($jobId, $label) {
    $hash = md5($this->base_url . $jobId . time());
    $post_title = str_replace([" ", "\t", "\n", "\r"], "_", $label);
    $post_title = preg_replace("/[^A-Za-z0-9_]/", "", $post_title);
    $post_title = substr($post_title, 0, 100) . '_' . $hash;

    return $post_title;
  }
}
