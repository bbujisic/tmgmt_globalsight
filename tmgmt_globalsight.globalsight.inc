<?php
/**
 * @file
 * Provides GlobalSight connector.
 *
 * Check @link http://www.globalsight.com/
 * GlobalSight @endlink for more info.
 *
 */

/**
 * GlobalSight connector.
 */
class TMGMTGlobalSightConnector {


  public $base_url = '';
  private $username = '';
  private $password = '';
  public $endpoint = '';
  private $proxyhost = FALSE; // ?
  private $proxyport = FALSE; // ?
  public $file_profile_id = ''; // ?

  public $webservice;

  /**
   * Construct the connector to GlobalSight service.
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

    // Protection for the case if some other module already uses nusoap.
    // But seriously @todo: Nusoap should be abandoned!
    if (!class_exists('nusoap_client')) {
      module_load_include('php', 'tmgmt_globalsight', 'lib/nusoap/lib/nusoap');
    }

    $this->webservice = new nusoap_client(GLOBALSIGHT_WSDL_URL, TRUE);
    $this->webservice->setEndpoint($this->endpoint);
  }

  /**
   * Login method sends access parameters to Globalsight and, upon success, receives access token.
   *
   * @return bool|mixed
   *  - FALSE: if login failed for any reason.
   *  - Access Token: if login succeeded.
   *
   * @todo: Current process does authorization on each page request. Try saving access token in database and reusing it
   *        in successive requests.
   */
  function login() {

    $this->webservice->setHTTPProxy($this->proxyhost, $this->proxyport);
    $params = array(
      'p_username' => $this->username,
      'p_password' => $this->password,
    );
    $result = $this->webservice->call('login', $params);

    if ($this->webservice->fault) {
      if (!($err = $this->webservice->getError())) {
        $err = 'No error details';
      }
      watchdog('tmgmt_globalsight', "Error connecting to GlobalSight. Please make sure access credentials for !gs were properly set. <b>Error message:</b> %err", array(
        '!gs' => l('GlobalSight', 'admin/config/regional/tmgmt_translator/manage/GlobalSight'),
        '%err' => $err
      ), WATCHDOG_ERROR);
      return FALSE;
    }

    return $result;
  }

  /**
   * getLocales method sends 'getFileProfileInfoEx' API request and parses a list of available languages.
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
        $locales['source'][] = (string)$profile->localeInfo->sourceLocale;
        foreach ($profile->localeInfo->targetLocale as $locale) {
          $locales['target'][] = (string)$locale;
        }
      }
    }

    return $locales;
  }

  /**
   * Method generates titles for GlobalSight by replacing unsupported characters with underlines and
   * adding some MD5 hash trails in order to assure uniqueness of job titles.
   *
   * @param TMGMTJob $job
   *   Loaded TMGMT Job object.
   * @return string
   *   GlobalSight job title.
   */
  function generateJobTitle($job) {
    $hash = md5($this->base_url . $job->tjid . time());

    if ($job->source_language == "en") {
      // use post title + hash
      $post_title = str_replace(array(" ", "\t", "\n", "\r"), "_", $job->label);
      $post_title = preg_replace("/[^A-Za-z0-9_]/", "", $post_title);
      $post_title = substr($post_title, 0, 50) . '_' . $hash;
    } else {
      $post_title = 'dp_' . $hash;
    }

    return $post_title;
  }

  /**
   * Method generates XML document for GlobalSight based on TMGMTJob object.
   *
   * @param TMGMTJob $job
   *   Loaded TMGMT Job object.
   * @return string
   *   XML document as per GlobalSight API specifications.
   */
  function encodeXML($job) {
    $strings = tmgmt_flatten_data($job->getData());

    $xml = "<?xml version='1.0' encoding='UTF-8' ?>";
    $xml .= "<fields id='$job->tjid'>";

    foreach ($strings as $key => $string) {
      if ((isset($string['#translate']) && $string['#translate']) || strpos($key, 'node_title') !== FALSE) {
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
   *  Send method encodes and sends translation job to GlobalSight service.
   * @todo: This method does not handle errors at all! See fault report in $response['faultstring']:
      com.globalsight.webservices.WebServiceException: <?xml version="1.0" encoding="UTF-8" ?>
      <errorXml>
        <method>uploadFileForInitial</method>
        <error>You cannot have \, /, :, ;, *, ?, |, ", &lt;, &gt;, % or &amp; in the Job Name.</error>
      </errorXml>
   *
   * @param TMGMTJob $job
   *   Loaded TMGMT Job object.
   * @param $target_locale
   *   GlobalSign locale code (e.g. en_US).
   * @param $name
   *   GlobalSight job title.
   * @return array
   *   Array of parameters sent with CreateJob API call.
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

  /**
   *
   * @param string $job_name
   *   GlobalSight job title.
   * @return mixed
   *  - FALSE : Ignore the status, move on...
   *  - "PERMANENT ERROR" : There is a permanent error at GS. Cancel the job.
   *  - API response converted to the array.
   */
  function getStatus($job_name) {
    if (!($access_token = $this->login())) {
      return FALSE;
    }

    $params = array(
      'p_accessToken' => $access_token,
      'p_jobName' => $job_name
    );
    $result = $this->webservice->call('getStatus', $params);

    if ($this->webservice->fault) {
      if (!($err = $this->webservice->getError())) {
        $err = 'No error details';
      }
      // I do not like watchdog here! Let's try and create an error handler class in any future refactor
      watchdog('tmgmt_globalsight', "Error getting job status for !job_name. Translation job will be canceled. <br> <b>Error message:</b><br> %err", array(
        '!job_name' => $job_name,
        '%err' => $err
      ), WATCHDOG_ERROR);
      return 'PERMANENT ERROR';
    }

    try {
      $xml = new SimpleXMLElement($result);
      return $this->xml2array($xml);
    } catch (Exception $err) {
      watchdog('tmgmt_globalsight', "Error parsing XML for !job_name. Translation job will be canceled. <br> <b>Error message:</b><br> %err", array(
        '!job_name' => $job_name,
        '%err' => $err
      ), WATCHDOG_ERROR);
      return 'PERMANENT ERROR';
    }

  }

  /**
   * Method cancel requests job deletion in GlobalSight.
   *
   * @param string $job_name
   *   GlobalSight job title.
   * @return mixed
   *  - FALSE: on any API error
   *  - API response in form of array
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
      watchdog('tmgmt_globalsight', "Could not cancel !job_name job. <br> <b>Error message:</b><br> %err", array(
        '!job_name' => $job_name,
        '%err' => $err
      ));
      return FALSE;
    }

    $xml = new SimpleXMLElement($result);
    return $this->xml2array($xml);
  }

  /**
   * This method downloads translations for a given GlobalSight job name.
   *
   * @param $job_name
   *   Title of the GlobalSight job
   * @return array|bool
   *   - FALSE: if API request failed due to any reason
   *   - API response in form of array
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
    $xml = new SimpleXMLElement($result);
    $download_url_prefix = (string)$xml->urlPrefix;

    $result = $this->webservice->call("getJobExportFiles", $params);
    $xml = new SimpleXMLElement($result);
    $paths = $xml->paths;

    $results = array();

    $http_options = array();

    // Create stream context.
    // @todo: Test this...
    if ($this->proxyhost && $this->proxyport) {
      $aContext = array(
        'http' => array(
          'proxy' => $this->proxyhost . ":" . $this->proxyport,
          'request_fulluri' => TRUE,
        ),
      );
      $http_options['context'] = stream_context_create($aContext);
    }
    else {
      $http_options['context'] = null;
    }

    foreach ($paths as $path) {
      $path = trim((string)$path);


      // We are going to presume that path /globalsight/cxedocs/Gcomp/fr_FR/webservice/226/GlobalSight.xml
      // is going to give us enough information to recognize the language, in this case fr_FR. The language
      // identifier will be the key in the returned array.
      $languageCode = trim(explode('/', $path)[0]);

      $url = $download_url_prefix . '/' . $path;

      // IDZ for some reason does not allow usage of drupal_http_request() function. So utterly ugly file_get_contents
      // is being used.
      $result = file_get_contents($url, FALSE, $http_options['context']);

      // @todo: Find a better plan for contingency if result code is not 200
      if ($result && $languageCode) {
        $xmlObject = simplexml_load_string($result, 'SimpleXMLElement', LIBXML_NOCDATA);
        foreach ($xmlObject->field as $field) {
          $value = (string)$field->value;
          $key = (string)$field->name;
          $results[$languageCode][$key] = array(
            '#text' => $value,
          );
        }
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

    return $a[intval($code)];
  }

  /**
   * Helper method recursively converting xml documents to array.
   */
  function xml2array($xmlObject, $out = array()) {
    foreach ((array)$xmlObject as $index => $node) {
      $out[$index] = (is_object($node)) ? $this->xml2array($node) : $node;
    }

    return $out;
  }

  /**
   * Method checks if job upload to GlobalSight succeeded.
   *
   * @param $jobName
   *   Title of the GlobalSight job
   *
   * @return bool
   *   TRUE: if job import succeeded
   *   FALSE: if job import failed
   */
  function uploadErrorHandler($jobName) {
    $status = $this->getStatus($jobName);
    $status = $status['status'];

    // LEVERAGING appears to be normal status right after successful upload

    switch ($status) {

      case 'LEVERAGING':
        return TRUE;
        break;

      // IMPORT_FAILED appears to be status when XML file is corrupt.
      case 'IMPORT_FAILED':
        watchdog('tmgmt_globalsight', "Error uploading file to GlobalSight. XML file appears to be corrupt or GlobalSight server timed out. Translation job canceled.", array(), WATCHDOG_ERROR);
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
          watchdog('tmgmt_globalsight', "Error creating job at GlobalSight. Translation job canceled.", array(), WATCHDOG_ERROR);
          drupal_set_message(t('Error creating job at GlobalSight. Translation job canceled.'), 'error');
          return FALSE;
        }
        break;
    };

    return TRUE;
  }
}
