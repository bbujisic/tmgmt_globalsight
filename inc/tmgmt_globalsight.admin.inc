<?php

function tmgmt_globalsight_admin_form() {
  $form = array();

  $form['sync'] = array(
    '#type' => 'fieldset',
    '#collapsible' => FALSE,
    '#title' => t('Sync translations'),
  );
  $form['sync']['download'] = array(
    '#type' => 'submit',
    '#value' => t('Download'),
    '#description' => t('Try to download translations manually.')
  );

  $form['tmgmt_globalsight_download_on_cron'] = array(
    '#type' => 'checkbox',
    '#default_value' => variable_get('tmgmt_globalsight_download_on_cron', 1),
    '#title' => t('Download on cron'),
    '#description' => t('Enabling this allows download of new translations during cron jobs.'),
  );

  $form['tmgmt_globalsight_localize_links'] = array(
    '#type' => 'checkbox',
    '#default_value' => variable_get('tmgmt_globalsight_localize_links', 0),
    '#title' => t('Localize links'),
    '#description' => t('Enable in order to allow replacement of hard coded localization paths (e.g. en/about-us to de/about-us).'),
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Submit'),
  );

  return $form;
}

function tmgmt_globalsight_admin_form_submit(&$form, &$form_state) {
  //dsm($form_state);
  switch ($form_state['values']['op']) {

    // Handles settings submission.
    case $form_state['values']['submit']:
      variable_set('tmgmt_globalsight_download_on_cron', $form_state['values']['tmgmt_globalsight_download_on_cron']);
      variable_set('tmgmt_globalsight_localize_links', $form_state['values']['tmgmt_globalsight_localize_links']);
      break;

    // Handles download button.
    case $form_state['values']['download']:
      tmgmt_globalsight_get_all();
      break;
  }


}

function tmgmt_globalsight_jobs_form() {


  // query
  $query = db_select('tmgmt_globalsight', 'gs');
  $query->join('tmgmt_job', 'j', 'gs.tjid = j.tjid');
  $query->fields('gs', array(
    'tjid',
    'status'
  ))
    ->fields('j', array(
      'label',
      'settings',
      'target_language'
    ));
  // ->condition('status', 1)

  $result = $query->execute();

  $form = array();

  $form['table'] = array(
    '#theme' => 'tmgmt_globalsight_form_table',
    '#tree' => TRUE,
    '#header' => array(
      t('Label'),
      t('Target languages'),
      t('Status')
    ),
  );


  foreach ($result as $record) {
    $settings = unserialize($record->settings);
    $record->target_languages = $settings['associated_jobs'];
    $record->target_languages[] = $record->target_language;

    $form['table']['rows'][$record->tjid] = array(
      'label' => array(
        '#markup' => l($record->label, 'admin/tmgmt/jobs/' . $record->tjid),
      ),
      'targets' => array(
        '#markup' => implode(', ', $record->target_languages),
      ),
      'status' => array(
        '#type' => 'select',
        '#options' => _tmgmt_globalsight_statuses($record->status),
        '#default_value' => $record->status
      )
    );
  }


  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Submit'),
  );

  return $form;
}

function tmgmt_globalsight_jobs_form_submit($form, $form_state) {
  //dsm($form_state);
  foreach ($form_state['values']['table']['rows'] as $tjid => $row) {
    if ($row['status'] != $form_state['complete form']['table']['rows'][$tjid]['status']['#default_value']) {
      // Update the status
      tmgmt_globalsight_set_status($tjid, $row['status']);


      // @todo: Should we also update the actual job? Or even make sure if the job exists?!
    }
  }
}

function theme_tmgmt_globalsight_form_table(&$variables) {
  // Get the useful values.
  $form = $variables['form'];
  $rows = $form['rows'];
  $header = $form['#header'];

  // Setup the structure to be rendered and returned.
  $content = array(
    '#theme' => 'table',
    '#header' => $header,
    '#rows' => array(),
  );

  // Traverse each row.  @see element_chidren().
  foreach (element_children($rows) as $row_index) {
    $row = array();
    // Traverse each column in the row.  @see element_children().
    foreach (element_children($rows[$row_index]) as $col_index) {
      // Render the column form element.
      $row[] = drupal_render($rows[$row_index][$col_index]);
    }
    // Add the row to the table.
    $content['#rows'][] = $row;
  }


  // Redner the table and return.
  return drupal_render($content);

}