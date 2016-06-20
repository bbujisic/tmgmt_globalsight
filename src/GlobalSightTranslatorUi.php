<?php

/**
 * @file
 * Contains \Drupal\tmgmt_globalsight\GlobalSightTranslatorUi.
 */
namespace Drupal\tmgmt_globalsight;

use Drupal\tmgmt\TranslatorPluginUiBase;
use Drupal\Core\Form\FormStateInterface;

/**
 * GlobalSight translator UI.
 */
class GlobalSightTranslatorUi extends TranslatorPluginUiBase {
	
	/**
	 *
	 * {@inheritdoc}
	 *
	 */
	public function buildConfigurationForm(array $form, FormStateInterface $form_state) {
		$form = parent::buildConfigurationForm ( $form, $form_state );
		
		/** @var \Drupal\tmgmt\TranslatorInterface $translator */
		$translator = $form_state->getFormObject ()->getEntity ();
		
		$form ['endpoint'] = array (
				'#type' => 'textfield',
				'#title' => t ( 'Webservice Endpoint URL' ),
				'#default_value' => $translator->getSetting ( 'endpoint' ),
				'#description' => t ( 'If you have not modified any configuration on your GlobalSight installation, this value should be http://globalsightip:port/globalsight/services/AmbassadorWebService' ) 
		);
		$form ['username'] = array (
				'#type' => 'textfield',
				'#title' => t ( 'GlobalSight username' ),
				'#default_value' => $translator->getSetting ( 'username' ) 
		);
		$form ['password'] = array (
				'#type' => 'textfield',
				'#title' => t ( 'GlobalSight password' ),
				'#default_value' => $translator->getSetting ( 'password' ) 
		);
		$form ['file_profile_name'] = array (
				'#type' => 'textfield',
				'#title' => t ( 'File profile name' ),
				'#default_value' => $translator->getSetting ( 'file_profile_name' ) 
		);
		return $form;
	}
}
