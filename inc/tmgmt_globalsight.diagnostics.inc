<?php

function tmgmt_globalsight_test() {
  $reportHeader = array(t('Component'), t('Message'));
  $report = _tmgmt_globalsight_diagnostics();

  return theme('table', array('header'=>$reportHeader, 'rows'=>$report));
}

function _tmgmt_globalsight_diagnostics() {
  $report = array();

  // Initiate Drupal side settings
  $translator = tmgmt_translator_load('gs');
  if ($translator->plugin == 'gs') {
    $report[] = array('Translator class', 'loaded');
  } else {
    $report[] = array('Translator class', 'Fatal error. GlobalSight translator class could not be loaded.');
    return $report;
  }
  $connector = new TMGMTGlobalSightConnector($translator);
  if ($connector) {
    $report[] = array('Connector class', 'loaded');
  } else {
    $report[] = array('Connector class', 'Fatal error. TMGMT GlobalSight connector class could not be loaded.');
    return $report;
  }
  $report[] = array('Base URL', $connector->base_url);
  $report[] = array('Endpoint', $connector->endpoint);
  $report[] = array('File profile ID', $connector->file_profile_id);

  //Ping endpoint
  $tB = microtime(true);
  $fP = fSockOpen($connector->endpoint);
  if (!$fP) {
    $report[] = array('fSockOpen()', 'Failed. This is not a problem as GS does not use fSockOpen, but it is a problem for Drupal');
  } else {
    $tA = microtime(true);
    $time = round((($tA - $tB) * 1000), 0) . " ms";
    $report[] = array('fSockOpen()', 'Ping succeeded with average of ' . $time);
  }

// Try cURL to endpoint
  $ch = curl_init($connector->endpoint);
  curl_setopt($ch, CURLOPT_TIMEOUT, 5);
  curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 5);
  curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
  $data = curl_exec($ch);
  $httpcode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
  curl_close($ch);
  if ($httpcode >= 200 && $httpcode < 300) {
    $report[] = array('cURL', 'Success. Server returned: <br><code><pre>' . $data . '</pre></code>');
  } else {
    $report[] = array('cURL', 'Failed. HTTP code: ' . $httpcode . '. Site could work without cURL... Watch for errors below.');
  }

  // Try to login
  $access_token = $connector->login();
  if ($access_token) {
    $report[] = array('GS Login', 'Login successful. Returned access token is:' . $access_token);
  } else {
    $report[] = array('GS Login', 'Fatal error: login failed.');
    $report[] = array('NuSoap debug string', '<pre>' . $connector->webservice->debug_str . '</pre>');
    return $report;
  }


  // Get list of locales
  $locs = $connector->getLocales();
  if (is_array($locs)) {
    $report[] = array('Supported source languages', implode(' , ', $locs['source']));
    $report[] = array('Supported target languages', implode(' , ', $locs['target']));
  } else {
    $report[] = array('Supported languages', 'ERROR - could not be retrieved');
  }

  return $report;
}