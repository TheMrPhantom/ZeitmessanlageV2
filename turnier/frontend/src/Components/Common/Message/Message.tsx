import { Typography } from '@mui/material'
import React from 'react'
import { useParams } from 'react-router-dom'
import Spacer from '../Spacer'
import { DANKE_FUER_ANMELDUNG, LASSE_ACCOUNT_ACTIVIEREN } from '../Internationalization/i18n'

type Props = {}

const Message = (props: Props) => {
    const params = useParams()
    const message = params.message ? params.message : ""

    const displayedMessage = () => {
        if (message === "new-user") {
            return <>
                <Spacer vertical={50} />
                <Typography variant='h4' textAlign={'center'}>{DANKE_FUER_ANMELDUNG}</Typography>
                <Spacer vertical={15} />
                <Typography variant='h6' textAlign={'center'}>{LASSE_ACCOUNT_ACTIVIEREN}</Typography>
            </>
        } else if (message === "activate") {
            return <>
                <Spacer vertical={50} />
                <Typography variant='h4' textAlign={'center'}>{LASSE_ACCOUNT_ACTIVIEREN}</Typography>
            </>
        }
    }

    return (
        <div style={{ "width": "100%", "padding": "15px" }}>
            {displayedMessage()}
        </div>
    )
}

export default Message